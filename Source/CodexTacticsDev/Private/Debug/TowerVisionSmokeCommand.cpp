// Dev-only check of game designer bug 09 (2026-11-01, L_FormationLabCombat in PIE, Saved/BugReport/2026-11-01_bug_09_tower_vision): an operative
// on top of a combat-lab tower had his vision cone drawn straight over a tall lab wall - he "saw" everything behind it. The cone is now the 2.5D
// sight rule from his real eye height (VisionConeVisibility, docs/systems/combat-and-fire.md "Elevated vision cones"):
//  1. the leader stands on the tower T_SW (4 m) facing the 3 m lab wall W_C_4: his cone is traced from the perch - its rays look down on the
//     ground 4 m below, the mesh lies on that ground;
//  2. along the ray to the wall: the roof edge's dead zone at the tower's foot is cut, the ground before the wall full, the wall's shadow cut
//     (WallShadowLengthCm), the ground beyond the shadow full again - the drawn level and UTacticalSightSubsystem::CanOperativeSee (3D traces
//     from his eye) agree at every one, and on a grid round the wall;
//  3. a ground-level operative at the tower's foot facing the same wall: the flat model (every ray's ground 0, no perch), nothing behind
//     the wall, his cone agrees with his sight on the grid too;
//  4. an enemy standing in the wall's shadow is not seen by the leader, one beyond the shadow is.
// Headless (-game, the map loaded from disk):
//   powershell -ExecutionPolicy Bypass -File Scripts/smoke_parallel.ps1 -Commands TowerVisionSmoke   (map + -LevelJson from test_map.json)
// Rendered still of the fixed cone (needs rendering, not -nullrhi; -RenderOffscreen works):
//   UnrealEditor.exe CodexTactics.uproject /Game/Maps/L_FormationLabCombat -game -RenderOffscreen -ResX=1600 -ResY=900 -LevelJson=L_FormationLabCombat.json
//     -ExecCmds="CodexTactics.TowerVisionSmoke Shot"   -> Saved/Screenshots/TowerVision/tower_vision_fixed.png (+ tower_vision_close.png)
// UE-only (no Godot reference).

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Camera/TacticalCameraPawn.h"
#include "Characters/EnemyCharacter.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/CombatPressureSubsystem.h"
#include "Combat/SightRules.h"
#include "Combat/TacticalSightSubsystem.h"
#include "Combat/VisionConeComponent.h"
#include "Combat/VisionConeVisibility.h"
#include "Combat/WaveSubsystem.h"
#include "Containers/Ticker.h"
#include "Debug/SmokeUtils.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/HUD.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/Paths.h"
#include "TimerManager.h"
#include "UnrealClient.h"

namespace TowerVisionSmoke
{
	constexpr float WarmupSeconds = 3.f;
	constexpr float Timeout = 120.f;
	/** Layout points (Content/Data/FormationLab: tower T_SW, wall W_C_4's middle). */
	const FVector2D TowerHint(-5200.f, -2200.f);
	const FVector2D WallHint(-3750.f, -1500.f);
	constexpr float ConeRangeCm = 3600.f;

	enum class EStage : uint8 { Setup, Settle, Checks, Enemies, EnemyCheck, ShotFrame, ShotTake, ShotCloseFrame, ShotCloseTake, Done };

	struct FState
	{
		EStage Stage = EStage::Setup;
		float Time = 0.f;
		float StageTime = 0.f;
		int32 Failures = 0;
		bool bShot = false;
		bool bShotRequested = false;
		FBox TowerBox = FBox(ForceInit);
		FBox WallBox = FBox(ForceInit);
		FVector Dir = FVector::ForwardVector;
		TWeakObjectPtr<AOperativeCharacter> Leader;
		TWeakObjectPtr<AOperativeCharacter> Grounded;
		TArray<TWeakObjectPtr<AEnemyCharacter>> Enemies;
		/** Distances along the leader's ray (planar, from his feet). */
		float Edge = 0.f;
		float WallNear = 0.f;
		float WallFar = 0.f;
		float ShadowPoint = 0.f;
		float BeyondPoint = 0.f;
		/** What the operatives had before (restored). */
		bool bLeaderRangeFromWeapon = true;
		float LeaderRange = 0.f;
		bool bGroundedRangeFromWeapon = true;
		float GroundedRange = 0.f;
	};

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	void Restore(FState& State)
	{
		UVisionConeComponent::SetPlayerSquadCones(false);
		if (IConsoleVariable* Conditions = IConsoleManager::Get().FindConsoleVariable(TEXT("Codex.Sight.Conditions")))
		{
			Conditions->Set(1, ECVF_SetByCode);
		}
		if (AOperativeCharacter* Leader = State.Leader.Get())
		{
			Leader->bVisionConeRangeFromWeapon = State.bLeaderRangeFromWeapon;
			Leader->VisionConeRangeCm = State.LeaderRange;
			Leader->bVisionConeShowsSightRange = true;
		}
		if (AOperativeCharacter* Grounded = State.Grounded.Get())
		{
			Grounded->bVisionConeRangeFromWeapon = State.bGroundedRangeFromWeapon;
			Grounded->VisionConeRangeCm = State.GroundedRange;
			Grounded->bVisionConeShowsSightRange = true;
		}
	}

	bool Finish(FState& State, bool bComplete)
	{
		Restore(State);
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 && bComplete ? TEXT("PASS") : TEXT("FAIL"));
		State.Stage = EStage::Done;
		FPlatformMisc::RequestExit(false, TEXT("TowerVisionSmoke"));
		return false;
	}

	void Enter(FState& State, EStage Stage)
	{
		State.Stage = Stage;
		State.StageTime = 0.f;
	}

	FVector FeetOf(const ACharacter& Character)
	{
		return Character.GetActorLocation() - FVector(0.f, 0.f, Character.GetSimpleCollisionHalfHeight());
	}

	/** Segment [0, MaxCm] from From along the planar Dir against a box footprint: entry / exit distances (From inside: entry 0). */
	bool Crossing(const FVector& From, const FVector& Dir, float MaxCm, const FBox& Box, float& OutNear, float& OutFar)
	{
		float Near = 0.f;
		float Far = MaxCm;
		for (int32 Axis = 0; Axis < 2; ++Axis)
		{
			const double D = Dir[Axis];
			const double Origin = From[Axis];
			if (FMath::Abs(D) < 1.e-8)
			{
				if (Origin < Box.Min[Axis] || Origin > Box.Max[Axis])
				{
					return false;
				}
				continue;
			}
			float T0 = static_cast<float>((Box.Min[Axis] - Origin) / D);
			float T1 = static_cast<float>((Box.Max[Axis] - Origin) / D);
			if (T0 > T1)
			{
				Swap(T0, T1);
			}
			Near = FMath::Max(Near, T0);
			Far = FMath::Min(Far, T1);
			if (Near > Far)
			{
				return false;
			}
		}
		OutNear = Near;
		OutFar = Far;
		return true;
	}

	/** The ground under a planar point (pawns ignored): false when what is there is not the lab floor (a wall's top, a roof). */
	bool GroundAt(UWorld* World, const FVector& Point, float GroundZ, FVector& OutGround)
	{
		FCollisionQueryParams Params(SCENE_QUERY_STAT(TowerVisionSmokeGround), false);
		for (TActorIterator<APawn> It(World); It; ++It)
		{
			Params.AddIgnoredActor(*It);
		}
		FHitResult Hit;
		if (!World->LineTraceSingleByChannel(Hit, FVector(Point.X, Point.Y, GroundZ + 800.f), FVector(Point.X, Point.Y, GroundZ - 300.f), ECC_Visibility, Params))
		{
			return false;
		}
		OutGround = Hit.ImpactPoint;
		return FMath::Abs(OutGround.Z - GroundZ) < 25.f;
	}

	/** What the squad's sight sees at a ground point: a standing enemy's profile (+1), a crouched one's (+1) - CanOperativeSee (3D traces). */
	int32 SightLevel(const UTacticalSightSubsystem& Sight, const AOperativeCharacter& Operative, const FVector& Ground)
	{
		int32 Level = 0;
		for (const EOperativeStance Stance : { EOperativeStance::Standing, EOperativeStance::Crouching })
		{
			Level += Sight.CanOperativeSee(Operative, Ground + FVector(0.f, 0.f, SightRules::ProfileHeight(Stance))) ? 1 : 0;
		}
		return Level;
	}

	/**
	 * Drawn level vs CanOperativeSee on a 100 cm grid round the ray to the wall (Along from Origin along Dir, Side across), inside the drawn
	 * sector, on the lab floor, stable within 25 cm (boundaries skipped). Returns the mismatches.
	 */
	int32 CompareGrid(UWorld* World, const FState& State, const UTacticalSightSubsystem& Sight, const AOperativeCharacter& Operative,
		const FVector& Origin, float AlongFrom, int32& OutPoints, int32 OutLevels[3], FString& OutFirst)
	{
		const UVisionConeComponent* Cone = Operative.VisionCone;
		const FVector Right = FVector::CrossProduct(FVector::UpVector, State.Dir).GetSafeNormal();
		const float GroundZ = static_cast<float>(State.TowerBox.Min.Z);
		const FVector Facing = FRotator(0.f, Cone->GetComponentRotation().Yaw, 0.f).Vector();
		const float Range = Cone->GetConeRangeCm();
		const float Half = Cone->GetConeHalfAngleDeg();
		int32 Mismatches = 0;
		for (float Along = AlongFrom; Along <= State.WallFar + 1800.f; Along += 100.f)
		{
			for (float Side = -1100.f; Side <= 1100.f; Side += 100.f)
			{
				const FVector Point = Origin + State.Dir * Along + Right * Side;
				const FVector To = (Point - Origin).GetSafeNormal2D();
				const float Distance = static_cast<float>(FVector::Dist2D(Point, Origin));
				FVector Ground;
				if (Distance > Range - 40.f || FVector::DotProduct(To, Facing) < FMath::Cos(FMath::DegreesToRadians(Half - 3.f)) || !GroundAt(World, Point, GroundZ, Ground))
				{
					continue;
				}
				const int32 Drawn = Cone->GetDrawnLevelAt(Ground);
				const int32 Seen = SightLevel(Sight, Operative, Ground);
				bool bStable = true;
				for (const FVector& Offset : { State.Dir * 25.f, State.Dir * -25.f, Right * 25.f, Right * -25.f })
				{
					FVector Near;
					bStable &= GroundAt(World, Ground + Offset, GroundZ, Near) && Cone->GetDrawnLevelAt(Near) == Drawn && SightLevel(Sight, Operative, Near) == Seen;
				}
				if (!bStable)
				{
					continue;
				}
				++OutPoints;
				++OutLevels[FMath::Clamp(Seen, 0, 2)];
				if (Drawn != Seen)
				{
					++Mismatches;
					if (OutFirst.IsEmpty())
					{
						OutFirst = FString::Printf(TEXT("(along %.0f, side %.0f): drawn %d, sight %d"), Along, Side, Drawn, Seen);
					}
				}
			}
		}
		return Mismatches;
	}

	/** Holds the scene: the leader on the roof facing the wall, the ground operative at the foot facing it, both cones on. */
	void HoldFacing(const FState& State)
	{
		for (const TWeakObjectPtr<AOperativeCharacter>& Weak : { State.Leader, State.Grounded })
		{
			if (AOperativeCharacter* Operative = Weak.Get())
			{
				Operative->SetActorRotation(State.Dir.Rotation());
			}
		}
	}

	/** The shot's camera: looking along Yaw (the tower -> the wall, like the designer's screenshot) at Point from Distance, no HUD. */
	void FocusCamera(UWorld* World, const FVector& Point, float Distance, float Yaw)
	{
		if (APlayerController* PC = UGameplayStatics::GetPlayerController(World, 0))
		{
			if (ATacticalCameraPawn* Camera = PC->GetPawn<ATacticalCameraPawn>())
			{
				Camera->SetCinematicLock(true);
				Camera->SetCinematicYaw(Yaw);
				Camera->SmoothFocusOnPosition(Point, 0.2f, Distance);
			}
			if (PC->MyHUD)
			{
				PC->MyHUD->bShowHUD = false;
			}
		}
	}

	FVector OnGround(const FState& State, float Along)
	{
		const AOperativeCharacter* Leader = State.Leader.Get();
		const FVector Feet = Leader ? FeetOf(*Leader) : FVector::ZeroVector;
		return FVector(Feet.X, Feet.Y, State.TowerBox.Min.Z) + State.Dir * Along;
	}

	bool Step(UWorld* World, FState& State, float Dt)
	{
		State.Time += Dt;
		State.StageTime += Dt;
		if (State.Time > Timeout)
		{
			Check(State, false, FString::Printf(TEXT("timeout in stage %d"), static_cast<int32>(State.Stage)));
			return Finish(State, false);
		}
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		UTacticalSightSubsystem* Sight = World->GetSubsystem<UTacticalSightSubsystem>();
		if (!Squad || !Sight || !Squad->GetLeader())
		{
			Check(State, false, TEXT("squad with a leader, the sight subsystem"));
			return Finish(State, false);
		}
		AOperativeCharacter* Leader = State.Leader.Get();
		AOperativeCharacter* Grounded = State.Grounded.Get();
		if (State.Stage != EStage::Setup && (!Leader || !Grounded))
		{
			Check(State, false, TEXT("the leader and the ground operative still there"));
			return Finish(State, false);
		}

		switch (State.Stage)
		{
		case EStage::Setup:
		{
			if (UCombatPressureSubsystem* Pressure = World->GetSubsystem<UCombatPressureSubsystem>())
			{
				Pressure->SetAutoSpawn(false);
				Pressure->DespawnAll();
			}
			// The tower T_SW and the wall W_C_4 (the layout's blocks: tagged FormationLab, the towers also CombatLabTower).
			float BestTower = MAX_flt;
			float BestWall = MAX_flt;
			for (TActorIterator<AStaticMeshActor> It(World); It; ++It)
			{
				if (!It->ActorHasTag(TEXT("FormationLab")))
				{
					continue;
				}
				const FBox Box = It->GetComponentsBoundingBox();
				const FVector2D Centre(Box.GetCenter().X, Box.GetCenter().Y);
				if (It->ActorHasTag(TEXT("CombatLabTower")))
				{
					const float Distance = static_cast<float>(FVector2D::Distance(Centre, TowerHint));
					if (Distance < BestTower)
					{
						BestTower = Distance;
						State.TowerBox = Box;
					}
				}
				else if (Box.GetSize().Z > 250.f && Box.GetSize().Z < 350.f)
				{
					const float Distance = static_cast<float>(FVector2D::Distance(Centre, WallHint));
					if (Distance < BestWall)
					{
						BestWall = Distance;
						State.WallBox = Box;
					}
				}
			}
			Check(State, BestTower < 300.f && BestWall < 300.f && State.TowerBox.GetSize().Z > 350.f,
				FString::Printf(TEXT("tower T_SW (%.0f cm off, %.0f cm tall) and wall W_C_4 (%.0f cm off, %.0f cm tall) found"), BestTower,
					State.TowerBox.GetSize().Z, BestWall, State.WallBox.GetSize().Z));
			const TArray<AOperativeCharacter*> Members = Squad->GetMembers();
			Leader = Squad->GetLeader();
			for (AOperativeCharacter* Member : Members)
			{
				if (Member && Member != Leader)
				{
					Grounded = Member;
					break;
				}
			}
			if (State.Failures > 0 || !Grounded)
			{
				Check(State, Grounded != nullptr, TEXT("a second operative"));
				return Finish(State, false);
			}
			State.Leader = Leader;
			State.Grounded = Grounded;
			Squad->EnterSoloMode();
			const FVector TowerTop(State.TowerBox.GetCenter().X, State.TowerBox.GetCenter().Y, State.TowerBox.Max.Z);
			const FVector WallMid = State.WallBox.GetCenter();
			State.Dir = FVector(WallMid.X - TowerTop.X, WallMid.Y - TowerTop.Y, 0.f).GetSafeNormal();
			// The leader on the roof's middle, standing, facing the wall; the second operative on the ground 4.5 m out, facing it too.
			Leader->StopOperative();
			Leader->SetStance(EOperativeStance::Standing);
			Leader->SetActorLocation(TowerTop + FVector(0.f, 0.f, Leader->GetSimpleCollisionHalfHeight() + 2.f), false, nullptr, ETeleportType::TeleportPhysics);
			Grounded->StopOperative();
			Grounded->SetStance(EOperativeStance::Standing);
			const FVector Foot = FVector(TowerTop.X, TowerTop.Y, State.TowerBox.Min.Z) + State.Dir * 450.f;
			Grounded->SetActorLocation(FVector(Foot.X, Foot.Y, Foot.Z + Grounded->GetSimpleCollisionHalfHeight() + 2.f), false, nullptr, ETeleportType::TeleportPhysics);
			State.bLeaderRangeFromWeapon = Leader->bVisionConeRangeFromWeapon;
			State.LeaderRange = Leader->VisionConeRangeCm;
			State.bGroundedRangeFromWeapon = Grounded->bVisionConeRangeFromWeapon;
			State.GroundedRange = Grounded->VisionConeRangeCm;
			for (AOperativeCharacter* Operative : { Leader, Grounded })
			{
				Operative->bVisionConeRangeFromWeapon = false;
				Operative->VisionConeRangeCm = ConeRangeCm; // reaches past the wall's shadow
				Operative->bVisionConeShowsSightRange = false; // the scene's own range (2026-11-04: the default cone shows the sight range)
			}
			// 2026-11-04: geometry, not the weather - the sight range without the weather / time-of-day multiplier (SightConditionsSmoke checks those).
			if (IConsoleVariable* Conditions = IConsoleManager::Get().FindConsoleVariable(TEXT("Codex.Sight.Conditions")))
			{
				Conditions->Set(0, ECVF_SetByCode);
			}
			UVisionConeComponent::SetPlayerSquadCones(true); // the player's Shift+O
			HoldFacing(State);
			UE_LOG(LogCodexTactics, Display, TEXT("[TowerVision] leader %s on the tower roof (z %.0f), %s at its foot, facing (%.2f, %.2f) the wall"),
				*Leader->GetName(), State.TowerBox.Max.Z, *Grounded->GetName(), State.Dir.X, State.Dir.Y);
			Enter(State, EStage::Settle);
			return true;
		}

		case EStage::Settle:
		{
			HoldFacing(State);
			if (State.StageTime < 2.f)
			{
				return true;
			}
			Enter(State, EStage::Checks);
			return true;
		}

		case EStage::Checks:
		{
			HoldFacing(State);
			const UVisionConeComponent* Cone = Leader->VisionCone;
			const FVector Feet = FeetOf(*Leader);
			const float GroundZ = static_cast<float>(State.TowerBox.Min.Z);
			Check(State, FMath::Abs(Feet.Z - State.TowerBox.Max.Z) < 20.f, FString::Printf(TEXT("the leader stands on the roof (feet %.0f cm over it)"),
				Feet.Z - State.TowerBox.Max.Z));
			Check(State, Cone && Cone->IsConeShown() && Cone->IsOccluded(), TEXT("his occluded cone is shown"));
			if (!Cone || !Cone->IsOccluded())
			{
				return Finish(State, false);
			}
			// 1. Traced from the perch: the rays look down on the ground 4 m below, the mesh lies on it.
			int32 Elevated = 0;
			for (const FVisionConeRay& Ray : Cone->GetBuiltRays())
			{
				Elevated += FMath::Abs(Ray.GroundCm - (GroundZ - Feet.Z)) < 15.f ? 1 : 0;
			}
			const int32 RayCount = Cone->GetBuiltRays().Num();
			Check(State, RayCount > 0 && Elevated == RayCount, FString::Printf(TEXT("every ray looks down on the ground %.0f cm below (%d / %d)"),
				Feet.Z - GroundZ, Elevated, RayCount));
			const FBox ConeBox = Cone->Bounds.GetBox();
			Check(State, FMath::Abs(ConeBox.Min.Z - GroundZ) < 15.f && ConeBox.Max.Z - GroundZ < 20.f,
				FString::Printf(TEXT("the cone is drawn on the ground (z %.0f .. %.0f, ground %.0f; it floated at the roof, %.0f)"), ConeBox.Min.Z, ConeBox.Max.Z,
					GroundZ, State.TowerBox.Max.Z));

			// 2. Along the ray to the wall: dead zone, before the wall, the shadow, beyond it.
			float Near = 0.f;
			const bool bTower = Crossing(Feet, State.Dir, 10000.f, State.TowerBox, Near, State.Edge);
			const bool bWall = Crossing(Feet, State.Dir, 10000.f, State.WallBox, State.WallNear, State.WallFar);
			const float EyeAboveRoof = SightRules::EyeHeight(EOperativeStance::Standing) + static_cast<float>(Feet.Z - State.TowerBox.Max.Z);
			const float Roof = static_cast<float>(State.TowerBox.Max.Z) - GroundZ;
			const float Eye = static_cast<float>(Feet.Z) + SightRules::EyeHeight(EOperativeStance::Standing) - GroundZ;
			const float WallTop = static_cast<float>(State.WallBox.Max.Z) - GroundZ;
			const float StandingProfile = SightRules::ProfileHeight(EOperativeStance::Standing);
			const float CrouchedProfile = SightRules::ProfileHeight(EOperativeStance::Crouching);
			const float DeadStanding = VisionConeVisibility::PerchDeadZoneCm(EyeAboveRoof, Roof, State.Edge, StandingProfile);
			const float DeadCrouched = VisionConeVisibility::PerchDeadZoneCm(EyeAboveRoof, Roof, State.Edge, CrouchedProfile);
			const float ShadowStanding = VisionConeVisibility::WallShadowLengthCm(Eye, WallTop, State.WallFar, StandingProfile);
			const float ShadowCrouched = VisionConeVisibility::WallShadowLengthCm(Eye, WallTop, State.WallFar, CrouchedProfile);
			UE_LOG(LogCodexTactics, Display, TEXT("[TowerVision] eye %.0f cm over the ground, roof %.0f, edge %.0f cm, dead zone %.0f / %.0f cm, wall %.0f cm tall at %.0f - %.0f cm, shadow of the ground %.0f cm (h_w x d / (h_e - h_w)), of a standing / crouched target %.0f / %.0f cm"),
				Eye, Roof, State.Edge, DeadStanding, DeadCrouched, WallTop, State.WallNear, State.WallFar,
				VisionConeVisibility::WallShadowLengthCm(Eye, WallTop, State.WallFar), ShadowStanding, ShadowCrouched);
			Check(State, bTower && bWall && Eye > WallTop && State.WallFar + ShadowCrouched + 300.f < Cone->GetConeRangeCm(),
				TEXT("the ray to the wall: he is on the tower, the wall is lower than his eye, the shadow ends inside his cone"));
			State.ShadowPoint = State.WallFar + 0.5f * ShadowStanding;
			State.BeyondPoint = State.WallFar + ShadowCrouched + 250.f;
			struct FProbe
			{
				const TCHAR* What;
				float Along;
				int32 Expected;
			};
			const FProbe Probes[] = {
				{ TEXT("the dead zone at the tower's foot (the roof edge hides it)"), 0.5f * (State.Edge + DeadStanding), 0 },
				{ TEXT("the ground before the wall"), 0.5f * (DeadCrouched + State.WallNear), 2 },
				{ TEXT("the wall's shadow (the designer's black triangle)"), State.ShadowPoint, 0 },
				{ TEXT("beyond the wall's shadow (he sees over the wall)"), State.BeyondPoint, 2 },
			};
			for (const FProbe& Probe : Probes)
			{
				FVector Ground;
				const bool bFloor = GroundAt(World, OnGround(State, Probe.Along), GroundZ, Ground);
				const int32 Drawn = bFloor ? Cone->GetDrawnLevelAt(Ground) : -1;
				const int32 Seen = bFloor ? SightLevel(*Sight, *Leader, Ground) : -1;
				Check(State, bFloor && Drawn == Probe.Expected && Seen == Probe.Expected,
					FString::Printf(TEXT("tower: %s, %.0f cm out: drawn %d, sight %d (expected %d)"), Probe.What, Probe.Along, Drawn, Seen, Probe.Expected));
			}
			int32 Points = 0;
			int32 Levels[3] = { 0, 0, 0 };
			FString First;
			const int32 Mismatches = CompareGrid(World, State, *Sight, *Leader, Feet, 200.f, Points, Levels, First);
			Check(State, Points >= 150 && Levels[0] > 10 && Levels[2] > 50 && Mismatches == 0,
				FString::Printf(TEXT("tower: drawn cone = his sight (CanOperativeSee) on a 1 m grid: %d points (cut %d, standing only %d, full %d), %d mismatches %s"),
					Points, Levels[0], Levels[1], Levels[2], Mismatches, *First));

			// 3. The ground-level operative: the flat model, nothing behind the 3 m wall.
			const UVisionConeComponent* GroundCone = Grounded->VisionCone;
			const FVector GroundFeet = FeetOf(*Grounded);
			Check(State, GroundCone && GroundCone->IsConeShown() && GroundCone->IsOccluded() && FMath::Abs(GroundFeet.Z - GroundZ) < 20.f,
				FString::Printf(TEXT("ground operative: on the floor (feet %.0f cm over it), occluded cone shown"), GroundFeet.Z - GroundZ));
			if (GroundCone && GroundCone->IsOccluded())
			{
				int32 Flat = 0;
				int32 Perches = 0;
				for (const FVisionConeRay& Ray : GroundCone->GetBuiltRays())
				{
					Flat += Ray.GroundCm == 0.f ? 1 : 0;
					for (const FVisionConeObstacle& Obstacle : Ray.Obstacles)
					{
						Perches += Obstacle.bHidesInside ? 1 : 0;
					}
				}
				Check(State, Flat == GroundCone->GetBuiltRays().Num() && Perches == 0,
					FString::Printf(TEXT("ground operative: the flat model (%d / %d rays at his own level, %d perch obstacles)"), Flat, GroundCone->GetBuiltRays().Num(), Perches));
				const FBox GroundBox = GroundCone->Bounds.GetBox();
				Check(State, FMath::Abs(GroundBox.Min.Z - GroundZ) < 15.f, FString::Printf(TEXT("ground operative: his cone at his feet (z %.0f)"), GroundBox.Min.Z));
				for (const float Along : { 0.5f * (450.f + State.WallNear), State.ShadowPoint, State.BeyondPoint })
				{
					FVector Ground;
					const FVector Point = OnGround(State, Along);
					const bool bFloor = GroundAt(World, Point, GroundZ, Ground);
					const int32 Expected = Along < State.WallNear ? 2 : 0;
					const int32 Drawn = bFloor ? GroundCone->GetDrawnLevelAt(Ground) : -1;
					const int32 Seen = bFloor ? SightLevel(*Sight, *Grounded, Ground) : -1;
					Check(State, bFloor && Drawn == Expected && Seen == Expected,
						FString::Printf(TEXT("ground operative: %.0f cm out from the tower (%s): drawn %d, sight %d (expected %d)"), Along,
							Along < State.WallNear ? TEXT("before the wall") : TEXT("behind the 3 m wall"), Drawn, Seen, Expected));
				}
				int32 GroundPoints = 0;
				int32 GroundLevels[3] = { 0, 0, 0 };
				FString GroundFirst;
				const int32 GroundMismatches = CompareGrid(World, State, *Sight, *Grounded, GroundFeet, 100.f, GroundPoints, GroundLevels, GroundFirst);
				Check(State, GroundPoints >= 100 && GroundLevels[0] > 10 && GroundMismatches == 0,
					FString::Printf(TEXT("ground operative: drawn cone = his sight on a 1 m grid: %d points (cut %d, standing only %d, full %d), %d mismatches %s"),
						GroundPoints, GroundLevels[0], GroundLevels[1], GroundLevels[2], GroundMismatches, *GroundFirst));
			}
			const FVisionConeBuildStats& Stats = UVisionConeComponent::GetBuildStats();
			UE_LOG(LogCodexTactics, Display, TEXT("[TowerVision] cone builds %d, rays %d, traces %d (%.1f traces / ray), task %.2f ms"), Stats.Builds, Stats.Rays, Stats.Traces,
				Stats.Rays > 0 ? static_cast<float>(Stats.Traces) / Stats.Rays : 0.f, Stats.TaskSeconds * 1000.0);
			Enter(State, State.bShot ? EStage::ShotFrame : EStage::Enemies);
			return true;
		}

		case EStage::ShotFrame:
		{
			// The designer's view: the tower, the wall, its shadow and the ground beyond; only the leader's cone.
			HoldFacing(State);
			UVisionConeComponent::SetPlayerSquadCones(false);
			Leader->bShowVisionCone = true;
			FocusCamera(World, OnGround(State, 0.5f * (State.WallFar + State.BeyondPoint) - 600.f), 4200.f, static_cast<float>(State.Dir.Rotation().Yaw));
			if (State.StageTime < 2.5f)
			{
				return true;
			}
			Enter(State, EStage::ShotTake);
			return true;
		}

		case EStage::ShotTake:
		case EStage::ShotCloseTake:
		{
			HoldFacing(State);
			if (!State.bShotRequested)
			{
				State.bShotRequested = true;
				const TCHAR* Name = State.Stage == EStage::ShotTake ? TEXT("tower_vision_fixed.png") : TEXT("tower_vision_close.png");
				const FString Path = FPaths::ProjectSavedDir() / TEXT("Screenshots") / TEXT("TowerVision") / Name;
				FScreenshotRequest::RequestScreenshot(Path, false, false);
				UE_LOG(LogCodexTactics, Display, TEXT("[TowerVision] screenshot requested: %s"), *Path);
				return true;
			}
			if (State.StageTime < 1.5f)
			{
				return true;
			}
			State.bShotRequested = false;
			Enter(State, State.Stage == EStage::ShotTake ? EStage::ShotCloseFrame : EStage::Enemies);
			return true;
		}

		case EStage::ShotCloseFrame:
		{
			HoldFacing(State);
			FocusCamera(World, OnGround(State, State.WallFar + 400.f), 2600.f, static_cast<float>(State.Dir.Rotation().Yaw) - 30.f);
			if (State.StageTime < 2.5f)
			{
				return true;
			}
			Enter(State, EStage::ShotCloseTake);
			return true;
		}

		case EStage::Enemies:
		{
			HoldFacing(State);
			Leader->bShowVisionCone = false;
			APlayerController* PC = UGameplayStatics::GetPlayerController(World, 0);
			if (ATacticalCameraPawn* Camera = PC ? PC->GetPawn<ATacticalCameraPawn>() : nullptr)
			{
				if (Camera->IsCinematicLocked())
				{
					Camera->SetCinematicLock(false); // the shot's view handed back
				}
			}
			UWaveSubsystem* Waves = World->GetSubsystem<UWaveSubsystem>();
			for (const float Along : { State.ShadowPoint, State.BeyondPoint })
			{
				FVector Ground;
				GroundAt(World, OnGround(State, Along), static_cast<float>(State.TowerBox.Min.Z), Ground);
				AEnemyCharacter* Enemy = Waves ? Waves->SpawnLevelEnemy(EEnemyArchetype::Frostbitten, Ground + FVector(0.f, 0.f, 95.f), (-State.Dir).Rotation()) : nullptr;
				State.Enemies.Add(Enemy);
			}
			Enter(State, EStage::EnemyCheck);
			return true;
		}

		case EStage::EnemyCheck:
		{
			HoldFacing(State);
			if (State.StageTime < 0.3f)
			{
				return true;
			}
			const FVector Feet = FeetOf(*Leader);
			for (int32 Index = 0; Index < State.Enemies.Num(); ++Index)
			{
				const AEnemyCharacter* Enemy = State.Enemies[Index].Get();
				const bool bInShadow = Index == 0;
				if (!Enemy)
				{
					Check(State, false, FString::Printf(TEXT("enemy %s spawned"), bInShadow ? TEXT("in the shadow") : TEXT("beyond the shadow")));
					continue;
				}
				const float Along = static_cast<float>(FVector::DotProduct(Enemy->GetActorLocation() - Feet, State.Dir));
				const bool bSeen = Sight->CanOperativeSee(*Leader, UTacticalSightSubsystem::ProfilePoint(*Enemy));
				Check(State, bSeen != bInShadow,
					FString::Printf(TEXT("an enemy %s (%.0f cm out, the shadow %.0f - %.0f cm): %s by the leader on the tower"),
						bInShadow ? TEXT("in the wall's shadow") : TEXT("beyond the shadow"), Along, State.WallFar, State.ShadowPoint * 2.f - State.WallFar,
						bSeen ? TEXT("seen") : TEXT("not seen")));
			}
			for (const TWeakObjectPtr<AEnemyCharacter>& Enemy : State.Enemies)
			{
				if (Enemy.IsValid())
				{
					Enemy->Destroy();
				}
			}
			return Finish(State, true);
		}

		default:
			return false;
		}
	}

	void Run(const TArray<FString>& Args, UWorld* World)
	{
		if (!World)
		{
			return;
		}
		TSharedRef<FState> State = MakeShared<FState>();
		State->bShot = Args.Num() > 0 && Args[0].Equals(TEXT("Shot"), ESearchCase::IgnoreCase);
		TWeakObjectPtr<UWorld> WeakWorld(World);
		FTimerHandle Handle;
		World->GetTimerManager().SetTimer(Handle, FTimerDelegate::CreateLambda([WeakWorld, State]()
		{
			FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakWorld, State](float)
			{
				UWorld* W = WeakWorld.Get();
				return W && State->Stage != EStage::Done && Step(W, *State, W->IsPaused() ? 0.f : W->GetDeltaSeconds());
			}), 0.f);
		}), WarmupSeconds, false);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.TowerVisionSmoke"),
		TEXT("Dev check (bug 09, 2026-11-01, L_FormationLabCombat): an operative on a tower sees over a lab wall only where the line clears it - ")
		TEXT("the cone on the ground below, the roof edge's dead zone and the wall's shadow cut, the drawn level = CanOperativeSee; an enemy in the shadow ")
		TEXT("unseen, one beyond it seen; a ground-level operative's cone the flat model. Arg Shot saves Saved/Screenshots/TowerVision/*.png. PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
