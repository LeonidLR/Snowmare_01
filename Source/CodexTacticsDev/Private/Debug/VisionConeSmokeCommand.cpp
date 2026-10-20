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
// Occlusion (game designer 2026-10-19, Commandos style): a Frostbitten guard with a 3 m wall and a 1.2 m wall in its cone
// (spawned cubes, the smoke's own arena): the cone is cut behind the tall wall (sample points not drawn and the game's
// sight trace blocked), drawn in the light «standing only» tone behind the low wall (a standing operative's sight line
// clear, a crouched one's blocked), full in the open; a 75 cm grid of the arena: the drawn level = what the game's sight
// trace sees (standing 150 cm / crouched 90 cm profiles at the perception ranges). Perf: 20 guards with cones on, turning
// (look-around 2 s), the rebuild cost per frame logged ([VisionCone] perf ...). Shot: also the designer's scene - the
// squad hiding behind the walls - Saved/Screenshots/vision_cone_occluded.png.
// The squad's cones (game designer 2026-10-20: «the obstacle isn't cut out of the operatives' vision cones»), Shift+O on: the
// leader crouched right behind a 0.8 m wall, a mate crouched right behind a 1.2 m wall, the third standing 5 m back, a 3 m wall
// 5 m beyond the low ones: every cone draws 2 levels; behind the walls the drawn level = UTacticalSightSubsystem::CanOperativeSee at a crouched
// (90 cm) and a standing (150 cm) enemy's profile (0.8 m: full for all - below every sight line; 1.2 m: cut for the crouched one,
// light «standing enemy only» for the standing one; 3 m: cut), and on a 75 cm grid of each cone; the cost of the second level
// (Codex.VisionCones.SquadLevels 1 vs 2) logged ([VisionCone] squad perf ...). Shot: also the designer's scene - one crouched
// behind the barricade, two standing 5 m back - Saved/Screenshots/squad_cones_occluded.png.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "AI/PerceptionRules.h"
#include "Camera/TacticalCameraPawn.h"
#include "Characters/EnemyCharacter.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/EnemyGhostActor.h"
#include "Combat/SightRules.h"
#include "Combat/SquadFireRules.h"
#include "Combat/TacticalSightSubsystem.h"
#include "Combat/VisionConeComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Combat/WaveSubsystem.h"
#include "Components/CapsuleComponent.h"
#include "Containers/Ticker.h"
#include "Data/WeaponDataAsset.h"
#include "Debug/SmokeUtils.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "GameFlow/LevelEncounterSubsystem.h"
#include "GameFramework/HUD.h"
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
		/** The smoke's arena (stage 0). */
		FVector Feet = FVector::ZeroVector;
		FVector F = FVector::ForwardVector;
		FVector R = FVector::RightVector;
		/** Occlusion stage: the Frostbitten guard, its walls, the perf crowd. */
		TWeakObjectPtr<AEnemyCharacter> Watcher;
		TArray<TWeakObjectPtr<AActor>> Spawned;
		TArray<TWeakObjectPtr<AEnemyCharacter>> Crowd;
		uint64 PerfFrame = 0;
		float PerfStart = 0.f;
		int32 PerfMode = 0;
		/** Squad-cone stage (2026-10-20): its walls, where the squad stood before, the level-cost measurement. */
		TArray<TWeakObjectPtr<AActor>> SquadWalls;
		TArray<FTransform> SavedSpots;
		TArray<EOperativeStance> SavedStances;
		int32 PerfLevels = 1;
		int32 PerfStep = 0;
	};

	/** A blocking cube (BasicShapes, BlockAll: blocks ECC_Visibility like a level wall). */
	AStaticMeshActor* SpawnBlock(UWorld* World, const FVector& Centre, const FRotator& Rotation, const FVector& SizeCm)
	{
		UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
		if (!Cube)
		{
			return nullptr;
		}
		const FTransform Transform(Rotation, Centre, SizeCm / 100.f);
		AStaticMeshActor* Block = World->SpawnActorDeferred<AStaticMeshActor>(AStaticMeshActor::StaticClass(), Transform, nullptr, nullptr,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (!Block)
		{
			return nullptr;
		}
		Block->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
		Block->GetStaticMeshComponent()->SetStaticMesh(Cube);
		Block->GetStaticMeshComponent()->SetCollisionProfileName(TEXT("BlockAll"));
		Block->FinishSpawning(Transform);
		return Block;
	}

	/** The game's sight trace (AEnemyCharacter::FindVisibleOperative): ECC_Visibility, simple collision, pawns / silhouettes ignored. */
	bool SightLineClear(UWorld* World, const FVector& From, const FVector& To)
	{
		FCollisionQueryParams Params(SCENE_QUERY_STAT(VisionConeSmokeSight), false);
		for (TActorIterator<APawn> It(World); It; ++It)
		{
			Params.AddIgnoredActor(*It);
		}
		for (TActorIterator<AEnemyGhostActor> It(World); It; ++It)
		{
			Params.AddIgnoredActor(*It);
		}
		FHitResult Hit;
		return !World->LineTraceSingleByChannel(Hit, From, To, ECC_Visibility, Params) || Cast<APawn>(Hit.GetActor()) != nullptr;
	}

	/** Ground under a point (false: none, or the top of an obstacle - not a spot an operative stands on). */
	bool GroundAt(UWorld* World, const FVector& Point, float FeetZ, FVector& OutGround)
	{
		FCollisionQueryParams Params(SCENE_QUERY_STAT(VisionConeSmokeGround), false);
		for (TActorIterator<APawn> It(World); It; ++It)
		{
			Params.AddIgnoredActor(*It);
		}
		FHitResult Hit;
		if (!World->LineTraceSingleByChannel(Hit, FVector(Point.X, Point.Y, FeetZ + 400.f), FVector(Point.X, Point.Y, FeetZ - 300.f), ECC_Visibility, Params))
		{
			return false;
		}
		OutGround = Hit.ImpactPoint;
		return FMath::Abs(OutGround.Z - FeetZ) < 25.f;
	}

	/** What the enemy's sight check sees at a ground point: standing operative (+1), crouched one (+1) (range + trace; no FOV). */
	int32 SightLevel(UWorld* World, const AEnemyCharacter& Enemy, const FVector& Ground)
	{
		const FEnemyPerceptionParams Params = Enemy.GetPerception();
		const FVector Eye = UTacticalSightSubsystem::EyePoint(Enemy);
		const float Distance = static_cast<float>(FVector::Dist2D(Enemy.GetActorLocation(), Ground));
		int32 Level = 0;
		for (const EOperativeStance Stance : { EOperativeStance::Standing, EOperativeStance::Crouching })
		{
			Level += Distance <= PerceptionRules::EffectiveSightRange(Params, Stance)
				&& SightLineClear(World, Eye, Ground + FVector(0.f, 0.f, SightRules::ProfileHeight(Stance))) ? 1 : 0;
		}
		return Level;
	}

	/** What the squad's sight sees at a ground point: a standing enemy's profile (+1), a crouched one's (+1) - CanOperativeSee (FOV, trace, peek). */
	int32 SquadSightLevel(const UTacticalSightSubsystem& Sight, const AOperativeCharacter& Operative, const FVector& Ground)
	{
		int32 Level = 0;
		for (const EOperativeStance Stance : { EOperativeStance::Standing, EOperativeStance::Crouching })
		{
			Level += Sight.CanOperativeSee(Operative, Ground + FVector(0.f, 0.f, SightRules::ProfileHeight(Stance))) ? 1 : 0;
		}
		return Level;
	}

	/** Point inside a cone's drawn sector (its origin / heading), RangeMarginCm short of its range and AngleMarginDeg inside its edges. */
	bool InsideDrawnSector(const UVisionConeComponent& Cone, const FVector& Point, float RangeMarginCm, float AngleMarginDeg)
	{
		const FVector Origin = Cone.GetComponentLocation();
		const FVector Facing = FRotator(0.f, Cone.GetComponentRotation().Yaw, 0.f).Vector();
		const FVector To = (Point - Origin).GetSafeNormal2D();
		return FVector::Dist2D(Point, Origin) <= Cone.GetConeRangeCm() - RangeMarginCm
			&& FVector::DotProduct(To, Facing) >= FMath::Cos(FMath::DegreesToRadians(Cone.GetConeHalfAngleDeg() - AngleMarginDeg));
	}

	/** The squad-cone stage's three: the leader, then the first two other members. */
	TArray<AOperativeCharacter*> SquadScene(const USquadSubsystem& Squad, AOperativeCharacter* Leader)
	{
		TArray<AOperativeCharacter*> Members;
		Members.Add(Leader);
		for (AOperativeCharacter* Member : Squad.GetMembers())
		{
			if (Member && Member != Leader && Members.Num() < 3)
			{
				Members.Add(Member);
			}
		}
		return Members;
	}

	/** Arena point: Along the arena's forward, Side to its right, from its origin. */
	FVector ArenaPoint(const FState& State, float Along, float Side)
	{
		return State.Feet + State.F * Along + State.R * Side;
	}

	/** A point of the watcher's cone frame (at its feet): AngleDeg from its facing (positive right), DistanceCm out. */
	FVector ConePoint(const AEnemyCharacter& Enemy, float AngleDeg, float DistanceCm)
	{
		const FVector Feet = Enemy.GetActorLocation() - FVector(0.f, 0.f, Enemy.GetSimpleCollisionHalfHeight());
		return Feet + Enemy.GetActorForwardVector().GetSafeNormal2D().RotateAngleAxis(AngleDeg, FVector::UpVector) * DistanceCm;
	}

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
			// Map-independent (the user's L_PatrolTest edits 2026-10-18): the squad, the brute and the hound go to an empty, flat,
			// navigable spot of the map (F -6 .. +17 m, R -6 .. +21 m), so no prop the designer moves stands between them.
			TArray<const AActor*> Ignore;
			for (const AOperativeCharacter* Member : Squad->GetMembers())
			{
				Ignore.Add(Member);
			}
			if (const APlayerController* PC = UGameplayStatics::GetPlayerController(World, 0))
			{
				Ignore.Add(PC->GetPawn());
			}
			const FVector LeaderFeet = Leader->GetActorLocation() - FVector(0.f, 0.f, Leader->GetSimpleCollisionHalfHeight());
			const SmokeUtils::FClearArena Arena = SmokeUtils::FindClearArena(World, LeaderFeet, Leader->GetActorForwardVector(),
				FBox2D(FVector2D(-600.f, -600.f), FVector2D(1700.f, 2100.f)), Ignore);
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke: arena %s at %s facing %s (%d tried, %d blocked by geometry, %d off the navmesh)"),
				Arena.bFound ? TEXT("found") : TEXT("NOT found"), *Arena.Origin.ToCompactString(), *Arena.Forward.ToCompactString(), Arena.Tried, Arena.Blocked, Arena.OffNav);
			if (!Arena.bFound)
			{
				Check(State, false, TEXT("an empty, flat, navigable spot on the map for the smoke's own set-up"));
				return Finish(State);
			}
			const FVector F = Arena.Forward;
			const FVector R = Arena.Right;
			const FVector Feet = Arena.Origin;
			State.Feet = Feet;
			State.F = F;
			State.R = R;
			int32 Index = 0;
			for (AOperativeCharacter* Member : Squad->GetMembers())
			{
				// The leader on the origin facing F, the others behind him (out of his cone).
				const FVector Spot = SmokeUtils::FreeSpot(World, Feet - F * (Member == Leader ? 0.f : 250.f) + R * (Member == Leader ? 0.f : (++Index == 1 ? -200.f : 200.f)), Member);
				Member->StopOperative();
				Member->TeleportTo(Spot, F.Rotation(), false, true);
			}
			// A brute 12.5 m straight ahead (well inside the leader's 14 m cone), a hound 18 m to the side; both face away (guarding).
			Brute = SpawnGuard(World, EEnemyArchetype::Brute, Feet + F * 1250.f, F);
			Hound = SpawnGuard(World, EEnemyArchetype::FrostHound, Feet + R * 1800.f, R);
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
			// 2026-10-20: the operative's cone is his field of view while the squad's sight runs (stance FOV 140 / 110 deg: +-70 / +-55);
			// the Godot +-35 deg (70 full) only without it.
			const float ExpectedHalf = Leader->GetVisionConeHalfAngle();
			Check(State, FMath::IsNearlyEqual(Cone->GetConeHalfAngleDeg(), ExpectedHalf, 0.01f)
				&& (FMath::IsNearlyEqual(ExpectedHalf, Leader->VisionConeHalfAngleDeg, 0.01f) || FMath::IsNearlyEqual(ExpectedHalf, 70.f, 0.01f) || FMath::IsNearlyEqual(ExpectedHalf, 55.f, 0.01f)),
				FString::Printf(TEXT("operative cone = his field of view: +-%.0f deg (Godot +-35 only without the FOV rule)"), Cone->GetConeHalfAngleDeg()));
			// Occluded since 2026-10-19: a fan of rays every 1.5 deg (+-35 deg: 48 base rays, more at shadow edges), not Godot's 32 segments.
			Check(State, Cone->IsOccluded() && Cone->GetBuiltRays().Num() >= 48 && Cone->GetFillVertexCount() > 0,
				FString::Printf(TEXT("occluded cone: %d rays, %d fill vertices"), Cone->GetBuiltRays().Num(), Cone->GetFillVertexCount()));
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
			State.Stage = 40;
			State.Time = 0.f;
			return true;
		}
		case 6:
		{
			// Occlusion: a Frostbitten guard 4 m behind / 9 m right of the squad's spot, facing the arena's forward; a 3 m wall
			// 6 m out at -25 deg (left), a 1.2 m wall 6 m out at +22 deg (right); open ground straight ahead.
			if (Brute)
			{
				Brute->Destroy();
			}
			if (Hound)
			{
				Hound->Destroy();
			}
			SetGlobal(-1);
			const FVector WatcherFeet = ArenaPoint(State, -400.f, 900.f);
			AEnemyCharacter* Watcher = SpawnGuard(World, EEnemyArchetype::Frostbitten, WatcherFeet, State.F);
			if (!Watcher)
			{
				Check(State, false, TEXT("frostbitten spawned"));
				return Finish(State);
			}
			Watcher->GuardLookAroundDeg = 0.f; // stares ahead: the walls stay where the checks expect them
			Watcher->bShowVisionCone = true;
			State.Watcher = Watcher;
			const float Yaw = static_cast<float>(State.F.Rotation().Yaw);
			auto WallAt = [&](float AngleDeg, const FVector& Size)
			{
				const FVector Dir = State.F.RotateAngleAxis(AngleDeg, FVector::UpVector);
				return SpawnBlock(World, WatcherFeet + Dir * (600.f + Size.X * 0.5f) + FVector(0.f, 0.f, Size.Z * 0.5f), FRotator(0.f, Yaw + AngleDeg, 0.f), Size);
			};
			State.Spawned.Add(WallAt(-25.f, FVector(40.f, 300.f, 300.f)));
			State.Spawned.Add(WallAt(22.f, FVector(40.f, 300.f, 120.f)));
			State.Stage = 7;
			State.Time = 0.f;
			return true;
		}
		case 7:
		{
			AEnemyCharacter* Watcher = State.Watcher.Get();
			if (!Watcher)
			{
				Check(State, false, TEXT("frostbitten alive"));
				return Finish(State);
			}
			if (State.Time < 1.f)
			{
				return true;
			}
			const UVisionConeComponent* Cone = Watcher->VisionCone;
			const float FeetZ = static_cast<float>(Watcher->GetActorLocation().Z - Watcher->GetSimpleCollisionHalfHeight());
			Check(State, Shown(Watcher) && Cone->IsOccluded() && Cone->GetLevelCount() == 2,
				FString::Printf(TEXT("frostbitten cone occluded, 2 levels (standing / crouched): %d rays, built %d times"), Cone->GetBuiltRays().Num(), Cone->GetBuildCount()));
			Check(State, Watcher->IsGuardingPost() && Flow->GetPhase() == ECodexGamePhase::Exploration, TEXT("the frostbitten guards calmly (nobody detected)"));
			// (a) behind the tall wall: not drawn, and the game's sight trace is blocked there.
			int32 Behind = 0;
			int32 BehindDrawn = 0;
			int32 BehindSeen = 0;
			for (float Angle = -32.f; Angle <= -18.f; Angle += 3.5f)
			{
				for (float Distance = 750.f; Distance <= 1250.f; Distance += 125.f)
				{
					FVector Ground;
					if (GroundAt(World, ConePoint(*Watcher, Angle, Distance), FeetZ, Ground))
					{
						++Behind;
						BehindDrawn += Cone->GetDrawnLevelAt(Ground) > 0 ? 1 : 0;
						BehindSeen += SightLevel(World, *Watcher, Ground) > 0 ? 1 : 0;
					}
				}
			}
			Check(State, Behind >= 20 && BehindDrawn == 0 && BehindSeen == 0,
				FString::Printf(TEXT("behind the 3 m wall: cut out (%d points: %d drawn, %d seen by the sight trace)"), Behind, BehindDrawn, BehindSeen));
			// (b) open ground ahead: drawn, full tone within the crouched range, and seen.
			int32 Open = 0;
			int32 OpenFull = 0;
			int32 OpenSeen = 0;
			for (float Angle = -6.f; Angle <= 6.f; Angle += 3.f)
			{
				for (float Distance = 300.f; Distance <= 1200.f; Distance += 150.f)
				{
					FVector Ground;
					if (GroundAt(World, ConePoint(*Watcher, Angle, Distance), FeetZ, Ground))
					{
						++Open;
						OpenFull += Cone->GetDrawnLevelAt(Ground) == 2 ? 1 : 0;
						OpenSeen += SightLevel(World, *Watcher, Ground) == 2 ? 1 : 0;
					}
				}
			}
			Check(State, Open >= 20 && OpenFull == Open && OpenSeen == Open,
				FString::Printf(TEXT("open ground ahead: full tone (%d points: %d full, %d seen standing + crouched)"), Open, OpenFull, OpenSeen));
			// (c) behind the 1.2 m wall: the light «standing only» tone; standing seen, crouched not.
			int32 Low = 0;
			int32 LowLight = 0;
			int32 LowStandingOnly = 0;
			for (float Angle = 15.f; Angle <= 29.f; Angle += 3.5f)
			{
				for (float Distance = 720.f; Distance <= 960.f; Distance += 60.f)
				{
					FVector Ground;
					if (GroundAt(World, ConePoint(*Watcher, Angle, Distance), FeetZ, Ground))
					{
						++Low;
						LowLight += Cone->GetDrawnLevelAt(Ground) == 1 ? 1 : 0;
						LowStandingOnly += SightLevel(World, *Watcher, Ground) == 1 ? 1 : 0;
					}
				}
			}
			Check(State, Cone->GetLightVertexCount() > 0 && Low >= 20 && LowLight == Low && LowStandingOnly == Low,
				FString::Printf(TEXT("behind the 1.2 m wall: «standing only» light tone (%d points: %d light, %d seen only standing; light vertices %d)"),
					Low, LowLight, LowStandingOnly, Cone->GetLightVertexCount()));
			// (d) the arena on a 75 cm grid: drawn level = the sight trace's level (points within 25 cm of a change skipped).
			const float Half = Cone->GetConeHalfAngleDeg();
			const float Range = Cone->GetConeRangeCm();
			const FVector WatcherFeet(Watcher->GetActorLocation().X, Watcher->GetActorLocation().Y, FeetZ);
			const FVector Facing = FVector(1.f, 0.f, 0.f).RotateAngleAxis(Cone->GetComponentRotation().Yaw, FVector::UpVector);
			int32 Grid = 0;
			int32 Mismatch = 0;
			int32 Levels[3] = { 0, 0, 0 };
			FString First;
			for (float Along = -550.f; Along <= 1650.f; Along += 75.f)
			{
				for (float Side = -550.f; Side <= 2050.f; Side += 75.f)
				{
					FVector Ground;
					const FVector Point = ArenaPoint(State, Along, Side);
					const FVector To = (Point - WatcherFeet).GetSafeNormal2D();
					const float Distance = static_cast<float>(FVector::Dist2D(Point, WatcherFeet));
					if (Distance < 100.f || Distance > Range - 30.f || FVector::DotProduct(To, Facing) < FMath::Cos(FMath::DegreesToRadians(Half - 3.f))
						|| !GroundAt(World, Point, FeetZ, Ground))
					{
						continue;
					}
					const int32 Drawn = Cone->GetDrawnLevelAt(Ground);
					const int32 Seen = SightLevel(World, *Watcher, Ground);
					bool bStable = true;
					for (const FVector& Offset : { State.F * 25.f, State.F * -25.f, State.R * 25.f, State.R * -25.f })
					{
						FVector Near;
						bStable &= GroundAt(World, Ground + Offset, FeetZ, Near) && Cone->GetDrawnLevelAt(Near) == Drawn && SightLevel(World, *Watcher, Near) == Seen;
					}
					if (!bStable)
					{
						continue;
					}
					++Grid;
					++Levels[FMath::Clamp(Seen, 0, 2)];
					if (Drawn != Seen)
					{
						++Mismatch;
						if (First.IsEmpty())
						{
							First = FString::Printf(TEXT("(%.0f, %.0f): drawn %d, sight %d"), Along, Side, Drawn, Seen);
						}
					}
				}
			}
			Check(State, Grid >= 150 && Levels[0] > 10 && Levels[1] > 10 && Levels[2] > 50 && Mismatch == 0,
				FString::Printf(TEXT("drawn cone = the sight trace on a 75 cm grid: %d points (cut %d, standing only %d, full %d), %d mismatches %s"),
					Grid, Levels[0], Levels[1], Levels[2], Mismatch, *First));
			State.Stage = State.bShot ? 30 : 8;
			State.Time = 0.f;
			return true;
		}
		case 8:
		{
			// Perf: 20 guards with cones on (O / debug 1), each looking round +-40 deg every 2 s (they keep turning: every cone
			// wants its 12 Hz rebuild), the walls in their cones.
			SetGlobal(1);
			for (int32 Index = 0; Index < 20; ++Index)
			{
				const FVector Spot = ArenaPoint(State, 350.f + 220.f * (Index / 5), 300.f + 380.f * (Index % 5));
				const FVector Facing = State.F.RotateAngleAxis(-60.f + 30.f * (Index % 5), FVector::UpVector);
				if (AEnemyCharacter* Guard = SpawnGuard(World, Index % 2 == 0 ? EEnemyArchetype::Frostbitten : EEnemyArchetype::Brute, Spot, Facing))
				{
					Guard->GuardLookAroundDeg = 40.f;
					Guard->GuardLookPeriodSeconds = 2.f;
					State.Crowd.Add(Guard);
				}
			}
			State.Stage = 9;
			State.Time = 0.f;
			return true;
		}
		case 9:
		{
			// Each rebuild mode in turn (Codex.VisionCones.Parallel 0 inline, 1 parallel + waited, 2 async - the default), 2.5 s each.
			IConsoleVariable* Parallel = IConsoleManager::Get().FindConsoleVariable(TEXT("Codex.VisionCones.Parallel"));
			if (State.Time < 1.f || !Parallel)
			{
				return true; // the first (urgent) builds of the new cones are not the steady cost
			}
			if (State.PerfFrame == 0)
			{
				Parallel->Set(State.PerfMode, ECVF_SetByConsole);
				UVisionConeComponent::ResetBuildStats();
				State.PerfFrame = GFrameCounter;
				State.PerfStart = State.Time;
				return true;
			}
			if (State.Time - State.PerfStart < 2.5f)
			{
				return true;
			}
			const FVisionConeBuildStats& Stats = UVisionConeComponent::GetBuildStats();
			const double Frames = static_cast<double>(FMath::Max<uint64>(GFrameCounter - State.PerfFrame, 1));
			const double Builds = static_cast<double>(FMath::Max<int64>(Stats.Builds, 1));
			int32 ShownCount = 0;
			for (const TWeakObjectPtr<AEnemyCharacter>& Guard : State.Crowd)
			{
				ShownCount += Guard.IsValid() && Guard->VisionCone->IsConeShown() ? 1 : 0;
			}
			const double MsPerFrame = Stats.Seconds * 1000.0 / Frames;
			UE_LOG(LogCodexTactics, Display, TEXT("[VisionCone] perf (Codex.VisionCones.Parallel %d): %d cones shown (%d guards turning), %.0f frames: %lld rebuilds in %lld batches (%.1f per frame, peak %d, deferred %lld, off screen %lld), %.0f rays / %.0f traces per rebuild, ray work %.3f ms per rebuild (%.3f ms per frame summed over the threads), game thread %.3f ms per frame = set-up / inline rays %.3f + mesh upload %.3f + waiting for the tasks %.3f"),
				State.PerfMode, ShownCount + 1, State.Crowd.Num(), Frames, Stats.Builds, Stats.Batches, Stats.Builds / Frames, Stats.PeakPerFrame, Stats.Deferred,
				Stats.SkippedOffscreen, Stats.Rays / Builds, Stats.Traces / Builds, Stats.TaskSeconds * 1000.0 / Builds, Stats.TaskSeconds * 1000.0 / Frames, MsPerFrame,
				(Stats.Seconds - Stats.ApplySeconds - Stats.WaitSeconds) * 1000.0 / Frames, Stats.ApplySeconds * 1000.0 / Frames, Stats.WaitSeconds * 1000.0 / Frames);
			Check(State, ShownCount == State.Crowd.Num() && State.Crowd.Num() == 20, FString::Printf(TEXT("perf crowd (mode %d): %d / 20 cones shown"), State.PerfMode, ShownCount));
			Check(State, Stats.Builds > 0 && Stats.PeakPerFrame <= 20, FString::Printf(TEXT("cones rebuild throttled (mode %d): %lld rebuilds in %.0f frames, peak %d per frame"),
				State.PerfMode, Stats.Builds, Frames, Stats.PeakPerFrame));
			Check(State, MsPerFrame < 6.0, FString::Printf(TEXT("cone rebuild cost (mode %d) %.3f ms per frame on the game thread (headless, 21 cones)"), State.PerfMode, MsPerFrame));
			++State.PerfMode;
			State.PerfFrame = 0;
			if (State.PerfMode <= 2)
			{
				return true;
			}
			Parallel->Set(2, ECVF_SetByConsole);
			return Finish(State);
		}
		case 40:
		{
			// The squad's cones (game designer 2026-10-20: «the obstacle isn't cut out of the operatives' vision cones»), Shift+O on: the
			// leader crouched right behind a 0.8 m wall, a mate crouched right behind a 1.2 m wall, the third standing 5 m back between
			// them, a 3 m wall 5 m beyond the low ones; all facing the arena's forward. Brute and hound out of the picture (no enemy: no fight).
			for (AEnemyCharacter* Enemy : { Brute, Hound })
			{
				if (Enemy)
				{
					Enemy->Destroy();
				}
			}
			State.Brute = nullptr;
			State.Hound = nullptr;
			const TArray<AOperativeCharacter*> Members = SquadScene(*Squad, Leader);
			if (Members.Num() < 3)
			{
				Check(State, false, FString::Printf(TEXT("squad cones: three operatives (%d)"), Members.Num()));
				State.Stage = 6;
				return true;
			}
			SetGlobal(-1);
			UVisionConeComponent::SetPlayerSquadCones(true);
			Squad->SetFollowersHolding(true);
			const float Yaw = static_cast<float>(State.F.Rotation().Yaw);
			const FVector Spots[3] = { ArenaPoint(State, 300.f, -350.f), ArenaPoint(State, 300.f, 250.f), ArenaPoint(State, -250.f, -50.f) };
			for (int32 Index = 0; Index < 3; ++Index)
			{
				AOperativeCharacter* Member = Members[Index];
				State.SavedSpots.Add(Member->GetActorTransform());
				State.SavedStances.Add(Member->GetStance());
				Member->StopOperative();
				Member->TeleportTo(SmokeUtils::FreeSpot(World, Spots[Index], Member), State.F.Rotation(), false, true);
				Member->SetStance(Index < 2 ? EOperativeStance::Crouching : EOperativeStance::Standing);
			}
			// The walls from where the crouched two really stand: front face 60 cm ahead of the capsule's centre, 30 cm thick, 3 m long.
			for (int32 Index = 0; Index < 2; ++Index)
			{
				const float Height = Index == 0 ? 80.f : 120.f;
				const FVector Feet = Members[Index]->GetActorLocation() - FVector(0.f, 0.f, Members[Index]->GetSimpleCollisionHalfHeight());
				State.SquadWalls.Add(SpawnBlock(World, Feet + State.F * 75.f + FVector(0.f, 0.f, Height * 0.5f), FRotator(0.f, Yaw, 0.f), FVector(30.f, 300.f, Height)));
			}
			State.SquadWalls.Add(SpawnBlock(World, ArenaPoint(State, 800.f, -50.f) + FVector(0.f, 0.f, 150.f), FRotator(0.f, Yaw, 0.f), FVector(40.f, 400.f, 300.f)));
			State.Stage = 41;
			State.Time = 0.f;
			return true;
		}
		case 41:
		{
			if (State.Time < 1.f)
			{
				return true;
			}
			for (AOperativeCharacter* Member : SquadScene(*Squad, Leader))
			{
				Member->VisionCone->RequestRebuild(); // traced with the walls in place
			}
			State.Stage = 42;
			State.Time = 0.f;
			return true;
		}
		case 42:
		{
			if (State.Time < 0.6f)
			{
				return true;
			}
			const UTacticalSightSubsystem* Sight = World->GetSubsystem<UTacticalSightSubsystem>();
			const TArray<AOperativeCharacter*> Members = SquadScene(*Squad, Leader);
			if (!Sight || Members.Num() < 3)
			{
				Check(State, false, TEXT("squad cones: sight subsystem and three operatives"));
				State.Stage = 43;
				return true;
			}
			const TCHAR* Names[3] = { TEXT("crouched behind the 0.8 m wall"), TEXT("crouched behind the 1.2 m wall"), TEXT("standing 5 m back") };
			for (int32 Index = 0; Index < 3; ++Index)
			{
				const AOperativeCharacter* Member = Members[Index];
				const UVisionConeComponent* Cone = Member->VisionCone;
				Check(State, Shown(Member) && Cone->IsOccluded() && Cone->GetLevelCount() == 2
					&& FMath::IsNearlyEqual(Cone->GetBuiltSpec().EyeHeightCm, SightRules::EyeHeight(Member->GetStance()), 1.f),
					FString::Printf(TEXT("Shift+O, %s (%s, in cover %d): occluded cone, %d levels (standing / crouched enemy), eyes %.0f cm, %d rays, %d light vertices"),
						Names[Index], *Member->GetName(), Member->bInCover ? 1 : 0, Cone->GetLevelCount(), Cone->GetBuiltSpec().EyeHeightCm, Cone->GetBuiltRays().Num(),
						Cone->GetLightVertexCount()));
			}
			// Region checks: Expect = the drawn level and the squad's sight (CanOperativeSee at a crouched / standing enemy's profile) there.
			auto Region = [&](int32 Who, float AlongFrom, float AlongTo, float SideFrom, float SideTo, int32 Expect, const TCHAR* What)
			{
				int32 Points = 0;
				int32 Drawn = 0;
				int32 Seen = 0;
				for (float Along = AlongFrom; Along <= AlongTo + 0.1f; Along += 50.f)
				{
					for (float Side = SideFrom; Side <= SideTo + 0.1f; Side += 50.f)
					{
						FVector Ground;
						if (!GroundAt(World, ArenaPoint(State, Along, Side), State.Feet.Z, Ground) || !InsideDrawnSector(*Members[Who]->VisionCone, Ground, 30.f, 3.f))
						{
							continue;
						}
						++Points;
						Drawn += Members[Who]->VisionCone->GetDrawnLevelAt(Ground) == Expect ? 1 : 0;
						Seen += SquadSightLevel(*Sight, *Members[Who], Ground) == Expect ? 1 : 0;
					}
				}
				Check(State, Points >= 6 && Drawn == Points && Seen == Points,
					FString::Printf(TEXT("%s: %s (%d points: %d drawn so, %d so by CanOperativeSee)"), Names[Who], What, Points, Drawn, Seen));
			};
			// Honest: a 0.8 m wall is below a crouched man's eyes (95 cm) and below both profiles - nothing behind it is hidden from him.
			Region(0, 450.f, 740.f, -450.f, -250.f, 2, TEXT("behind the 0.8 m wall FULL (even a crouched enemy seen over it)"));
			// The 1.2 m wall is above his eyes: a crouched enemy is hidden behind it, a standing one only right behind it shows his head.
			Region(1, 450.f, 900.f, 150.f, 350.f, 0, TEXT("behind the 1.2 m wall CUT"));
			// 5 m back his eyes are at 160: the 1.2 m wall hides a crouched enemy up to ~8.7 m beyond his feet, a standing one is seen.
			Region(2, 460.f, 760.f, 250.f, 350.f, 1, TEXT("behind the 1.2 m wall LIGHT (only a standing enemy seen)"));
			Region(2, 450.f, 740.f, -400.f, -250.f, 2, TEXT("behind the 0.8 m wall FULL"));
			for (int32 Index = 0; Index < 3; ++Index)
			{
				Region(Index, 900.f, 1100.f, -120.f, -20.f, 0, TEXT("behind the 3 m wall CUT"));
			}
			// Each cone on a 75 cm grid: the drawn level = the squad's sight rule (points within 25 cm of a change skipped).
			for (int32 Index = 0; Index < 3; ++Index)
			{
				const AOperativeCharacter* Member = Members[Index];
				int32 Grid = 0;
				int32 Mismatch = 0;
				int32 Levels[3] = { 0, 0, 0 };
				FString First;
				for (float Along = -550.f; Along <= 1650.f; Along += 75.f)
				{
					for (float Side = -550.f; Side <= 1100.f; Side += 75.f)
					{
						FVector Ground;
						if (!GroundAt(World, ArenaPoint(State, Along, Side), State.Feet.Z, Ground) || !InsideDrawnSector(*Member->VisionCone, Ground, 30.f, 3.f)
							|| FVector::Dist2D(Ground, Member->VisionCone->GetComponentLocation()) < 100.f)
						{
							continue;
						}
						const int32 Drawn = Member->VisionCone->GetDrawnLevelAt(Ground);
						const int32 Seen = SquadSightLevel(*Sight, *Member, Ground);
						bool bStable = true;
						for (const FVector& Offset : { State.F * 25.f, State.F * -25.f, State.R * 25.f, State.R * -25.f })
						{
							FVector Near;
							bStable &= GroundAt(World, Ground + Offset, State.Feet.Z, Near) && Member->VisionCone->GetDrawnLevelAt(Near) == Drawn
								&& SquadSightLevel(*Sight, *Member, Near) == Seen;
						}
						if (!bStable)
						{
							continue;
						}
						++Grid;
						++Levels[FMath::Clamp(Seen, 0, 2)];
						if (Drawn != Seen && Mismatch++ == 0)
						{
							First = FString::Printf(TEXT("(%.0f, %.0f): drawn %d, sight %d"), Along, Side, Drawn, Seen);
						}
					}
				}
				// The crouched one hugging the 1.2 m wall (it covers +-68 deg of his +-70 deg view, above his 95 cm eyes) sees nothing beyond it.
				const bool bSpread = Index == 1 ? Levels[0] > 100 : Levels[0] > 0 && Levels[2] > 50 && (Index != 2 || Levels[1] > 0);
				Check(State, Grid >= 100 && bSpread && Mismatch == 0,
					FString::Printf(TEXT("%s: drawn cone = CanOperativeSee on a 75 cm grid: %d points (cut %d, standing only %d, full %d), %d mismatches %s"),
						Names[Index], Grid, Levels[0], Levels[1], Levels[2], Mismatch, *First));
			}
			State.PerfLevels = 1;
			State.PerfStep = 0;
			State.Stage = 44;
			State.Time = 0.f;
			return true;
		}
		case 44:
		{
			// Cost of the second level: the three cones re-traced 10 times with 1 level (the old cone), then 10 times with 2.
			IConsoleVariable* Levels = IConsoleManager::Get().FindConsoleVariable(TEXT("Codex.VisionCones.SquadLevels"));
			const TArray<AOperativeCharacter*> Members = SquadScene(*Squad, Leader);
			if (!Levels)
			{
				Check(State, false, TEXT("console variable Codex.VisionCones.SquadLevels"));
				State.Stage = 43;
				return true;
			}
			if (State.PerfStep == 0)
			{
				Levels->Set(State.PerfLevels, ECVF_SetByConsole);
				State.PerfStep = 1;
				return true; // the spec change rebuilds them once (not counted)
			}
			if (State.PerfStep == 1)
			{
				UVisionConeComponent::ResetBuildStats();
			}
			if (State.PerfStep <= 10)
			{
				for (AOperativeCharacter* Member : Members)
				{
					Member->VisionCone->RequestRebuild();
				}
				++State.PerfStep;
				return true;
			}
			const FVisionConeBuildStats& Stats = UVisionConeComponent::GetBuildStats();
			const double Builds = static_cast<double>(FMath::Max<int64>(Stats.Builds, 1));
			UE_LOG(LogCodexTactics, Display, TEXT("[VisionCone] squad perf (Codex.VisionCones.SquadLevels %d): %lld rebuilds of 3 cones, %.0f rays / %.0f traces per rebuild, ray work %.3f ms per rebuild (task threads), game thread %.3f ms per rebuild (set-up + mesh upload %.3f)"),
				State.PerfLevels, Stats.Builds, Stats.Rays / Builds, Stats.Traces / Builds, Stats.TaskSeconds * 1000.0 / Builds, Stats.Seconds * 1000.0 / Builds,
				Stats.ApplySeconds * 1000.0 / Builds);
			Check(State, Stats.Builds >= 15, FString::Printf(TEXT("squad perf (%d levels): %lld rebuilds measured"), State.PerfLevels, Stats.Builds));
			bool bLevels = true;
			for (const AOperativeCharacter* Member : Members)
			{
				bLevels &= Member->VisionCone->GetLevelCount() == State.PerfLevels;
			}
			Check(State, bLevels, FString::Printf(TEXT("Codex.VisionCones.SquadLevels %d: the squad's cones draw %d level(s)"), State.PerfLevels, State.PerfLevels));
			if (State.PerfLevels == 1)
			{
				State.PerfLevels = 2;
				State.PerfStep = 0;
				return true;
			}
			State.Stage = State.bShot ? 45 : 43;
			State.Time = 0.f;
			return true;
		}
		case 45:
		{
			// The designer's scene (screenshot 2026-10-20): one operative crouched right behind the barricade (the 1.2 m wall), two standing
			// ~5 m back facing it; the 0.8 m and the 3 m walls in view; Shift+O on, O off.
			const TArray<AOperativeCharacter*> Members = SquadScene(*Squad, Leader);
			if (Members.Num() >= 3)
			{
				Members[0]->StopOperative();
				Members[0]->TeleportTo(SmokeUtils::FreeSpot(World, ArenaPoint(State, -250.f, 550.f), Members[0]), (ArenaPoint(State, 375.f, 250.f) - ArenaPoint(State, -250.f, 550.f)).Rotation(), false, true);
				Members[0]->SetStance(EOperativeStance::Standing);
				Members[2]->SetActorRotation((ArenaPoint(State, 375.f, 250.f) - Members[2]->GetActorLocation()).GetSafeNormal2D().Rotation());
			}
			if (APlayerController* PC = UGameplayStatics::GetPlayerController(World, 0))
			{
				if (ATacticalCameraPawn* Camera = PC->GetPawn<ATacticalCameraPawn>())
				{
					Camera->SmoothFocusOnPosition(ArenaPoint(State, 450.f, 150.f), 0.2f, 2300.f);
				}
				if (PC->MyHUD)
				{
					PC->MyHUD->bShowHUD = false;
				}
			}
			State.Stage = 46;
			State.Time = 0.f;
			return true;
		}
		case 46:
		{
			if (State.Time < 2.f)
			{
				// Held on the scene (the camera would otherwise follow the teleported leader).
				if (APlayerController* PC = UGameplayStatics::GetPlayerController(World, 0))
				{
					if (ATacticalCameraPawn* Camera = PC->GetPawn<ATacticalCameraPawn>())
					{
						Camera->SmoothFocusOnPosition(ArenaPoint(State, 450.f, 150.f), 0.2f, 2300.f);
					}
				}
				return true;
			}
			const FString Path = FPaths::ProjectSavedDir() / TEXT("Screenshots") / TEXT("squad_cones_occluded.png");
			FScreenshotRequest::RequestScreenshot(Path, false, false);
			UE_LOG(LogCodexTactics, Display, TEXT("[VisionCone] screenshot requested: %s"), *Path);
			State.Stage = 47;
			State.Time = 0.f;
			return true;
		}
		case 47:
		{
			if (State.Time < 1.5f)
			{
				return true;
			}
			State.Stage = 43;
			return true;
		}
		case 43:
		{
			// Back to the set-up: walls gone, the squad where (and how) it stood, the toggle off, 2 levels.
			if (IConsoleVariable* Levels = IConsoleManager::Get().FindConsoleVariable(TEXT("Codex.VisionCones.SquadLevels")))
			{
				Levels->Set(2, ECVF_SetByConsole);
			}
			UVisionConeComponent::SetPlayerSquadCones(false);
			for (const TWeakObjectPtr<AActor>& Wall : State.SquadWalls)
			{
				if (Wall.IsValid())
				{
					Wall->Destroy();
				}
			}
			State.SquadWalls.Reset();
			const TArray<AOperativeCharacter*> Members = SquadScene(*Squad, Leader);
			for (int32 Index = 0; Index < Members.Num() && Index < State.SavedSpots.Num(); ++Index)
			{
				Members[Index]->StopOperative();
				Members[Index]->SetStance(State.SavedStances[Index]);
				Members[Index]->TeleportTo(State.SavedSpots[Index].GetLocation(), State.SavedSpots[Index].Rotator(), false, true);
			}
			Squad->SetFollowersHolding(false);
			if (APlayerController* PC = UGameplayStatics::GetPlayerController(World, 0))
			{
				if (PC->MyHUD)
				{
					PC->MyHUD->bShowHUD = true;
				}
			}
			State.Stage = 6;
			State.Time = 0.f;
			return true;
		}
		case 30:
		{
			// The designer's shot: the squad hides behind the walls - the leader and a mate behind the tall wall, the rest
			// crouched against the low one (inside its «crouched hidden» band) - the camera over the frostbitten's cone.
			AEnemyCharacter* Watcher = State.Watcher.Get();
			if (!Watcher)
			{
				return Finish(State);
			}
			Squad->SetFollowersHolding(true); // they stay where they hide (no walking back into formation)
			Leader->bShowVisionCone = false; // only the frostbitten's cone in the picture
			int32 Index = 0;
			for (AOperativeCharacter* Member : Squad->GetMembers())
			{
				const bool bTall = Index < 2;
				const float Angle = bTall ? -27.f + 6.f * Index : 16.f + 6.f * (Index - 2);
				const FVector Spot = ConePoint(*Watcher, Angle, bTall ? 760.f : 700.f) + FVector(0.f, 0.f, 92.f);
				Member->StopOperative();
				Member->TeleportTo(Spot, (-State.F).Rotation(), false, true);
				Member->SetStance(bTall ? EOperativeStance::Standing : EOperativeStance::Crouching);
				++Index;
			}
			if (APlayerController* PC = UGameplayStatics::GetPlayerController(World, 0))
			{
				if (ATacticalCameraPawn* Camera = PC->GetPawn<ATacticalCameraPawn>())
				{
					Camera->SmoothFocusOnPosition(ConePoint(*Watcher, 0.f, 600.f), 0.2f, 2600.f);
				}
				if (PC->MyHUD)
				{
					PC->MyHUD->bShowHUD = false; // a clean still: no feed / squad panel
				}
			}
			State.Stage = 31;
			State.Time = 0.f;
			return true;
		}
		case 31:
		{
			if (State.Time < 2.f)
			{
				return true;
			}
			const FString Path = FPaths::ProjectSavedDir() / TEXT("Screenshots") / TEXT("vision_cone_occluded.png");
			FScreenshotRequest::RequestScreenshot(Path, false, false);
			UE_LOG(LogCodexTactics, Display, TEXT("[VisionCone] screenshot requested: %s"), *Path);
			State.Stage = 32;
			State.Time = 0.f;
			return true;
		}
		case 32:
		{
			if (State.Time < 1.5f)
			{
				return true;
			}
			if (const AEnemyCharacter* Watcher = State.Watcher.Get())
			{
				Check(State, Watcher->IsGuardingPost(), TEXT("shot: the squad behind the walls stays unseen"));
			}
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
			// After the all-cones still: the occlusion checks and the designer's scene.
			SetGlobal(-1);
			State.Stage = 4;
			State.Time = 0.f;
			return true;
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
		TEXT("Dev check: vision cones of operatives and enemies (component, per-actor toggle, geometry from weapon / perception, colours, Codex.Debug.VisionCones, occlusion by walls = the sight trace, the squad's two-level cones = CanOperativeSee, rebuild cost); arg Shot saves vision_cones.png + vision_cone_occluded.png + squad_cones_occluded.png; PASS / FAIL."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
