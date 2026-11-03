// Dev-only check of the game designer's report of 2026-11-02 on the generated map L_Gen_Depot_1 (the Engineer PRONE at a roof edge as a sniper,
// vision cones on): (a) his cone still covered the ground behind the walls below - every wall's shadow must be cut, and what the cone shows
// must equal what he sees (UTacticalSightSubsystem::CanOperativeSee); (b) the sight RANGE grows with the height level (3 m steps of the eye
// over the ground below: +1 x1.25, +2 x1.5, +3 x1.75, max x2; the ground and the -3 m cut x1 - numbers pending the user's confirmation);
// (c) one vision model for the squad and the armed human enemies (the Marksman, the PMC fighters). docs/systems/sight-and-vision-cones.md
// "Sight from a height".
//  1. the leader prone at the west edge of the +3 hall B_hall_n (9 m), facing the 3 m office B_office_s: level 3, x1.75 (105 m); his cone
//     traced from the perch (the roof is several slabs, its walls end flush with it); a target in the office's shadow is not seen and not
//     drawn, one beyond the shadow is; the drawn cone = CanOperativeSee on a 1 m grid (0 mismatches); a target in the open 62-76 m out is
//     seen from the roof (beyond the 60 m ground range), not with Codex.Sight.HeightRange 0;
//  2. an operative in the -3 m cut: level 0, the ground range (60 m): a target 50 m along the cut seen, one 70 m out with a clear line not;
//  3. an operative on the ground at the same distance from that target with a clear line: not seen (beyond the ground range);
//  4. an operative prone on the ruin's +2 roof (over its interior wall's top): level 2, x1.5, his cone traced from the perch, = his sight;
//  5. a Marksman standing on the hall's roof spots an operative at the elevated range (45 m x1.75), not one behind the office, not the same
//     operative from the ground (45 m); a frost hound up there gets no bonus (a beast); the PMC fighter class is an armed human.
// Headless (-game, the map loaded from disk):
//   powershell -Command "& Scripts/smoke_parallel.ps1 -Commands VisionHeightSmoke"   (map + -LevelJson from test_map.json)
// Rendered stills of the roof sniper's cone (needs rendering; -RenderOffscreen works):
//   UnrealEditor.exe CodexTactics.uproject /Game/Maps/Generated/L_Gen_Depot_1 -game -RenderOffscreen -ResX=1600 -ResY=900 -LevelJson=L_Gen_Depot_1.json
//     -ExecCmds="CodexTactics.VisionHeightSmoke Shot"   -> Saved/Screenshots/VisionHeight/roof_sniper_cone.png, roof_sniper_shadow.png
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
#include "Data/EnemyPerception.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/HUD.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/Paths.h"
#include "TimerManager.h"
#include "UnrealClient.h"

namespace VisionHeightSmoke
{
	constexpr float WarmupSeconds = 3.f;
	constexpr float Timeout = 240.f;
	/** Layout points of gen_ref_01_02_depot_1 (cm, ground 0): the hall B_hall_n's west edge, the office B_office_s west of it, the ruin, the cut. */
	const FVector2D SniperSpot(3210.f, -1500.f); // 10 cm inside the roof slab's edge, over its 30 cm wall's top (flush with the roof)
	constexpr float SniperYaw = 180.f;
	constexpr float OfficeFarFaceX = 1485.f;   // B_office_s west wall's outer face (x 1500, 30 cm thick)
	constexpr float OfficeTopCm = 300.f;
	const FVector2D HallInside(4500.f, -1500.f); // on the hall's floor, under its roof (hidden from its roof)
	const FVector2D RuinSpot(-2010.f, 2253.f);  // the ruin's +2 roof at its edge: over its perimeter wall's and its upper interior wall's tops
	constexpr float RuinYaw = 0.f;
	const FVector2D CutSpot(0.f, 0.f);          // the -3 m cut's floor (the trench runs along y)
	constexpr float SniperConeCm = 4000.f;
	constexpr float RuinConeCm = 3000.f;

	enum class EStage : uint8 { Setup, Settle, RoofChecks, RoofOldSampler, ShotFrame, ShotTake, ShotCloseFrame, ShotCloseTake, CutChecks, GroundRange, RuinSettle, RuinChecks, Enemies, Done };

	struct FSavedCone
	{
		TWeakObjectPtr<AOperativeCharacter> Operative;
		bool bFromWeapon = true;
		float RangeCm = 0.f;
	};

	struct FState
	{
		EStage Stage = EStage::Setup;
		float Time = 0.f;
		float StageTime = 0.f;
		int32 Failures = 0;
		bool bShot = false;
		bool bShotRequested = false;
		TWeakObjectPtr<AOperativeCharacter> Sniper;
		TWeakObjectPtr<AOperativeCharacter> Mate;
		TWeakObjectPtr<AOperativeCharacter> Third;
		TArray<FSavedCone> Saved;
		float HallRoofZ = 0.f;
		float RuinRoofZ = 0.f;
		float CutFloorZ = 0.f;
		/** The open target 62-76 m from the sniper (feet on the street) and a ground spot at the same distance with a clear line to it. */
		FVector Target = FVector::ZeroVector;
		FVector GroundSpot = FVector::ZeroVector;
		bool bTarget = false;
		bool bGroundSpot = false;
		FVector ShadowPoint = FVector::ZeroVector;
		FVector BeyondPoint = FVector::ZeroVector;
		TArray<TWeakObjectPtr<AEnemyCharacter>> Spawned;
		/** Where the checked operative lay when his cone's rebuild was requested (a prone man's dead zone is ~30 x his distance to the edge). */
		FVector RebuildFeet = FVector::ZeroVector;
		int32 Rebuilds = 0;
	};

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	void SetHeightRangeCVar(int32 Value)
	{
		if (IConsoleVariable* Var = IConsoleManager::Get().FindConsoleVariable(TEXT("Codex.Sight.HeightRange")))
		{
			Var->Set(Value, ECVF_SetByCode);
		}
	}

	void SetElevatedFixCVar(int32 Value)
	{
		if (IConsoleVariable* Var = IConsoleManager::Get().FindConsoleVariable(TEXT("Codex.VisionCones.ElevatedFix")))
		{
			Var->Set(Value, ECVF_SetByCode);
		}
	}

	void Restore(FState& State)
	{
		SetElevatedFixCVar(1);
		UVisionConeComponent::SetPlayerSquadCones(false);
		UVisionConeComponent::SetPlayerEnemyCones(false);
		SetHeightRangeCVar(1);
		for (const FSavedCone& Saved : State.Saved)
		{
			if (AOperativeCharacter* Operative = Saved.Operative.Get())
			{
				Operative->bVisionConeRangeFromWeapon = Saved.bFromWeapon;
				Operative->VisionConeRangeCm = Saved.RangeCm;
				Operative->bShowVisionCone = false;
			}
		}
		for (const TWeakObjectPtr<AEnemyCharacter>& Enemy : State.Spawned)
		{
			if (Enemy.IsValid())
			{
				Enemy->Destroy();
			}
		}
	}

	bool Finish(FState& State, bool bComplete)
	{
		Restore(State);
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 && bComplete ? TEXT("PASS") : TEXT("FAIL"));
		State.Stage = EStage::Done;
		FPlatformMisc::RequestExit(false, TEXT("VisionHeightSmoke"));
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

	FCollisionQueryParams NoPawns(UWorld* World)
	{
		FCollisionQueryParams Params(SCENE_QUERY_STAT(VisionHeightSmoke), false);
		for (TActorIterator<APawn> It(World); It; ++It)
		{
			Params.AddIgnoredActor(*It);
		}
		return Params;
	}

	/** The top surface under a planar point (pawns ignored), traced from 25 m up. */
	bool SurfaceAt(UWorld* World, const FVector2D& Point, float& OutZ)
	{
		FHitResult Hit;
		if (!World->LineTraceSingleByChannel(Hit, FVector(Point, 2500.f), FVector(Point, -1500.f), ECC_Visibility, NoPawns(World)))
		{
			return false;
		}
		OutZ = static_cast<float>(Hit.ImpactPoint.Z);
		return true;
	}

	/** The open ground (the street, z within 25 cm of GroundZ) under a planar point: false under a roof / on a cover / in the cut. */
	bool GroundAt(UWorld* World, const FVector& Point, float GroundZ, FVector& OutGround)
	{
		float Z = 0.f;
		if (!SurfaceAt(World, FVector2D(Point.X, Point.Y), Z) || FMath::Abs(Z - GroundZ) >= 25.f)
		{
			return false;
		}
		OutGround = FVector(Point.X, Point.Y, Z);
		return true;
	}

	/** Puts an operative's feet on a surface point (his current capsule), facing Yaw, stopped. */
	void Place(AOperativeCharacter& Operative, const FVector& Feet, float Yaw)
	{
		if (Operative.bInCover)
		{
			Operative.LeaveCover(TEXT("VisionHeightSmoke")); // in cover he would look along the wall, not where the scene faces him
		}
		Operative.ClearPendingCover();
		Operative.StopOperative();
		Operative.SetActorLocationAndRotation(Feet + FVector(0.f, 0.f, Operative.GetSimpleCollisionHalfHeight() + 2.f), FRotator(0.f, Yaw, 0.f), false, nullptr,
			ETeleportType::TeleportPhysics);
	}

	void PlaceEnemy(AEnemyCharacter& Enemy, const FVector& Feet, float Yaw)
	{
		Enemy.SetActorLocationAndRotation(Feet + FVector(0.f, 0.f, Enemy.GetSimpleCollisionHalfHeight() + 2.f), FRotator(0.f, Yaw, 0.f), false, nullptr,
			ETeleportType::TeleportPhysics);
	}

	void ConeOn(FState& State, AOperativeCharacter& Operative, float RangeCm)
	{
		bool bKnown = false;
		for (const FSavedCone& Saved : State.Saved)
		{
			bKnown |= Saved.Operative.Get() == &Operative;
		}
		if (!bKnown)
		{
			State.Saved.Add({ &Operative, Operative.bVisionConeRangeFromWeapon, Operative.VisionConeRangeCm });
		}
		Operative.bVisionConeRangeFromWeapon = false;
		Operative.VisionConeRangeCm = RangeCm;
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
	 * Drawn level vs CanOperativeSee on a 1 m grid over his whole cone (street-level ground only, stable within 25 cm - boundaries skipped).
	 * Returns the mismatches; logs the first few.
	 */
	int32 CompareGrid(UWorld* World, const UTacticalSightSubsystem& Sight, const AOperativeCharacter& Operative, float GroundZ, int32& OutPoints,
		int32 OutLevels[3], FString& OutFirst, int32* OutCovered = nullptr)
	{
		const UVisionConeComponent* Cone = Operative.VisionCone;
		const FVector Origin = Cone->GetComponentLocation();
		const FVector Facing = FRotator(0.f, Cone->GetComponentRotation().Yaw, 0.f).Vector();
		const FVector Right = FVector::CrossProduct(FVector::UpVector, Facing).GetSafeNormal();
		const float Range = Cone->GetConeRangeCm();
		const float Half = Cone->GetConeHalfAngleDeg();
		int32 Mismatches = 0;
		for (float Along = 100.f; Along <= Range; Along += 100.f)
		{
			for (float Side = -Range; Side <= Range; Side += 100.f)
			{
				const FVector Point = Origin + Facing * Along + Right * Side;
				const float Distance = static_cast<float>(FVector::Dist2D(Point, Origin));
				FVector Ground;
				if (Distance > Range - 40.f || FVector::DotProduct((Point - Origin).GetSafeNormal2D(), Facing) < FMath::Cos(FMath::DegreesToRadians(Half - 3.f))
					|| !GroundAt(World, Point, GroundZ, Ground))
				{
					continue;
				}
				const int32 Drawn = Cone->GetDrawnLevelAt(Ground);
				const int32 Seen = SightLevel(Sight, Operative, Ground);
				bool bStable = true;
				for (const FVector& Offset : { Facing * 25.f, Facing * -25.f, Right * 25.f, Right * -25.f })
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
					if (OutCovered && Drawn > Seen)
					{
						++*OutCovered; // drawn although his sight does not reach it (the report: the cone over the walls' shadows)
					}
					if (Mismatches < 6)
					{
						UE_LOG(LogCodexTactics, Display, TEXT("[VisionHeight] mismatch at (%.0f, %.0f): %.0f cm out, drawn %d, sight %d"), Ground.X, Ground.Y, Distance, Drawn, Seen);
					}
					if (OutFirst.IsEmpty())
					{
						OutFirst = FString::Printf(TEXT("(%.0f, %.0f) drawn %d sight %d"), Ground.X, Ground.Y, Drawn, Seen);
					}
					++Mismatches;
				}
			}
		}
		return Mismatches;
	}

	/** Share of his cone's rays that look down on ground at least ExpectDropCm below his feet (traced from the perch). */
	void PerchedRays(const AOperativeCharacter& Operative, float ExpectDropCm, int32& OutElevated, int32& OutRays)
	{
		OutElevated = 0;
		OutRays = 0;
		for (const FVisionConeRay& Ray : Operative.VisionCone->GetBuiltRays())
		{
			++OutRays;
			OutElevated += Ray.GroundCm <= -ExpectDropCm + 30.f ? 1 : 0;
		}
	}

	/** What blocks the line (pawns / silhouettes ignored), for the log. */
	FString Blocker(UWorld* World, const FVector& From, const FVector& To)
	{
		FHitResult Hit;
		if (!World->LineTraceSingleByChannel(Hit, From, To, ECC_Visibility, NoPawns(World)))
		{
			return TEXT("nothing");
		}
		return FString::Printf(TEXT("%s at (%.0f, %.0f, %.0f)"), Hit.GetActor() ? *Hit.GetActor()->GetName() : TEXT("?"), Hit.ImpactPoint.X, Hit.ImpactPoint.Y,
			Hit.ImpactPoint.Z);
	}

	/** Requests a rebuild of his cone from where he lies now. */
	void RequestFreshCone(FState& State, AOperativeCharacter& Operative)
	{
		Operative.VisionCone->RequestRebuild();
		State.RebuildFeet = FeetOf(Operative);
	}

	/**
	 * His cone is traced from where he lies and the way he faces (0.3 s after the request he has not moved since by more than 2 cm, the cone's
	 * heading is his); else a new request (up to 20).
	 * False while waiting.
	 */
	bool IsConeFresh(FState& State, AOperativeCharacter& Operative)
	{
		if (State.StageTime < 0.3f)
		{
			return false;
		}
		const float ConeYaw = static_cast<float>(Operative.VisionCone->GetComponentRotation().Yaw);
		const bool bTurned = !Operative.VisionCone->HasViewOverride()
			&& FMath::Abs(FMath::FindDeltaAngleDegrees(ConeYaw, static_cast<float>(Operative.GetActorRotation().Yaw))) > 0.5f;
		if ((FVector::Dist(FeetOf(Operative), State.RebuildFeet) > 2.f || bTurned) && State.Rebuilds < 20)
		{
			++State.Rebuilds;
			RequestFreshCone(State, Operative);
			State.StageTime = 0.f;
			return false;
		}
		return true;
	}

	void HideFromRoof(UWorld* World, AOperativeCharacter& Operative, float Offset)
	{
		float Z = 0.f;
		SurfaceAt(World, HallInside + FVector2D(0.f, Offset), Z);
		Place(Operative, FVector(HallInside.X, HallInside.Y + Offset, Z), 0.f);
	}

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

	void ReleaseCamera(UWorld* World)
	{
		APlayerController* PC = UGameplayStatics::GetPlayerController(World, 0);
		if (ATacticalCameraPawn* Camera = PC ? PC->GetPawn<ATacticalCameraPawn>() : nullptr)
		{
			if (Camera->IsCinematicLocked())
			{
				Camera->SetCinematicLock(false);
			}
		}
	}

	/** The sniper on his roof spot (re-snapped: the prone capsule), facing the office. */
	void HoldSniper(UWorld* World, FState& State)
	{
		if (AOperativeCharacter* Sniper = State.Sniper.Get())
		{
			const FVector Feet = FeetOf(*Sniper);
			if (FVector::Dist2D(Feet, FVector(SniperSpot, 0.f)) > 10.f || FMath::Abs(Feet.Z - State.HallRoofZ) > 10.f)
			{
				Place(*Sniper, FVector(SniperSpot, State.HallRoofZ), SniperYaw);
			}
			Sniper->SetActorRotation(FRotator(0.f, SniperYaw, 0.f));
		}
	}

	/** A street spot Distance from Target with room to stand and a clear standing line to it (the ground-level comparison). */
	bool FindGroundSpot(UWorld* World, const UTacticalSightSubsystem& Sight, const FVector& Target, float Distance, FVector& OutSpot)
	{
		const FVector Profile = Target + FVector(0.f, 0.f, SightRules::ProfileHeight(EOperativeStance::Standing));
		const FCollisionQueryParams Params = NoPawns(World);
		for (float Bearing = 0.f; Bearing < 360.f; Bearing += 2.f)
		{
			FVector Ground;
			if (!GroundAt(World, Target + FRotator(0.f, Bearing, 0.f).Vector() * Distance, 0.f, Ground))
			{
				continue;
			}
			// Room to stand, a little away from walls (no automatic cover snap there): a 90 cm radius capsule touches nothing.
			if (World->OverlapAnyTestByChannel(Ground + FVector(0.f, 0.f, 100.f), FQuat::Identity, ECC_Pawn, FCollisionShape::MakeCapsule(90.f, 90.f), Params))
			{
				continue;
			}
			if (Sight.HasClearSight(Ground + FVector(0.f, 0.f, SightRules::EyeHeight(EOperativeStance::Standing)), Profile))
			{
				OutSpot = Ground;
				return true;
			}
		}
		return false;
	}

	/**
	 * The open target: a street point 62-68 m from the sniper (beyond the squad's 60 m ground range and the Marksman's, within both elevated ranges)
	 * inside +-24 deg of his facing, seen from his prone eye and from a standing eye there - preferably one with a street spot the same distance away
	 * that has a clear line to it (FindGroundSpot).
	 */
	bool FindRangeTarget(UWorld* World, const UTacticalSightSubsystem& Sight, const AOperativeCharacter& Sniper, FState& State)
	{
		const FVector Eye = UTacticalSightSubsystem::EyePoint(Sniper);
		const FVector StandingEye = FVector(SniperSpot, State.HallRoofZ + SightRules::EyeHeight(EOperativeStance::Standing));
		bool bAny = false;
		for (float Distance = 6200.f; Distance <= 6800.f; Distance += 150.f)
		{
			for (const float Off : { 0.f, 4.f, -4.f, 8.f, -8.f, 12.f, -12.f, 16.f, -16.f, 20.f, -20.f, 24.f, -24.f })
			{
				const FVector Dir = FRotator(0.f, SniperYaw + Off, 0.f).Vector();
				FVector Ground;
				if (!GroundAt(World, FVector(SniperSpot, 0.f) + Dir * Distance, 0.f, Ground))
				{
					continue;
				}
				const FVector Profile = Ground + FVector(0.f, 0.f, SightRules::ProfileHeight(EOperativeStance::Standing));
				if (!Sight.HasClearSight(Eye, Profile) || !Sight.HasClearSight(StandingEye, Profile))
				{
					continue;
				}
				if (!bAny)
				{
					bAny = true;
					State.Target = Ground;
				}
				FVector Spot;
				if (FindGroundSpot(World, Sight, Ground, static_cast<float>(FVector::Dist2D(Ground, FVector(SniperSpot, 0.f))), Spot))
				{
					State.Target = Ground;
					State.GroundSpot = Spot;
					State.bGroundSpot = true;
					return true;
				}
			}
		}
		return bAny;
	}

	float YawTo(const FVector& From, const FVector& To)
	{
		return static_cast<float>((To - From).GetSafeNormal2D().Rotation().Yaw);
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
		AOperativeCharacter* Sniper = State.Sniper.Get();
		AOperativeCharacter* Mate = State.Mate.Get();
		AOperativeCharacter* Third = State.Third.Get();
		if (State.Stage != EStage::Setup && (!Sniper || !Mate || !Third))
		{
			Check(State, false, TEXT("the three operatives still there"));
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
			const bool bHall = SurfaceAt(World, SniperSpot, State.HallRoofZ);
			const bool bRuin = SurfaceAt(World, RuinSpot, State.RuinRoofZ);
			FHitResult Hit;
			const bool bCut = World->LineTraceSingleByChannel(Hit, FVector(CutSpot, 50.f), FVector(CutSpot, -1000.f), ECC_Visibility, NoPawns(World));
			State.CutFloorZ = bCut ? static_cast<float>(Hit.ImpactPoint.Z) : 0.f;
			Check(State, bHall && FMath::Abs(State.HallRoofZ - 900.f) < 15.f, FString::Printf(TEXT("the hall B_hall_n's roof on +3 (z %.0f)"), State.HallRoofZ));
			Check(State, bRuin && FMath::Abs(State.RuinRoofZ - 600.f) < 15.f, FString::Printf(TEXT("the ruin's roof on +2 (z %.0f)"), State.RuinRoofZ));
			Check(State, bCut && FMath::Abs(State.CutFloorZ + 300.f) < 15.f, FString::Printf(TEXT("the cut's floor on -1 (z %.0f)"), State.CutFloorZ));
			const TArray<AOperativeCharacter*> Members = Squad->GetMembers();
			TArray<AOperativeCharacter*> Others;
			for (AOperativeCharacter* Member : Members)
			{
				if (Member && Member != Squad->GetLeader())
				{
					Others.Add(Member);
				}
			}
			if (State.Failures > 0 || Others.Num() < 2)
			{
				Check(State, Others.Num() >= 2, TEXT("three operatives"));
				return Finish(State, false);
			}
			Sniper = Squad->GetLeader();
			Mate = Others[0];
			Third = Others[1];
			State.Sniper = Sniper;
			State.Mate = Mate;
			State.Third = Third;
			Squad->EnterSoloMode();
			Sniper->SetStance(EOperativeStance::Prone);
			Mate->SetStance(EOperativeStance::Standing);
			Third->SetStance(EOperativeStance::Standing);
			Place(*Sniper, FVector(SniperSpot, State.HallRoofZ), SniperYaw);
			Place(*Mate, FVector(CutSpot, State.CutFloorZ), 90.f);
			HideFromRoof(World, *Third, 0.f);
			ConeOn(State, *Sniper, SniperConeCm);
			UVisionConeComponent::SetPlayerSquadCones(true);
			UE_LOG(LogCodexTactics, Display, TEXT("[VisionHeight] sniper %s prone on the hall roof (%.0f, %.0f, %.0f) facing %.0f; %s in the cut, %s inside the hall"),
				*Sniper->GetName(), SniperSpot.X, SniperSpot.Y, State.HallRoofZ, SniperYaw, *Mate->GetName(), *Third->GetName());
			Enter(State, EStage::Settle);
			return true;
		}

		case EStage::Settle:
		{
			HoldSniper(World, State);
			if (State.StageTime < 2.5f)
			{
				return true;
			}
			RequestFreshCone(State, *Sniper); // traced from where he lies now (a prone man's dead zone is ~30 x his distance to the edge)
			Enter(State, EStage::RoofChecks);
			return true;
		}

		case EStage::RoofChecks:
		{
			HoldSniper(World, State);
			if (!IsConeFresh(State, *Sniper))
			{
				return true;
			}
			const FVector Feet = FeetOf(*Sniper);
			Check(State, Sniper->GetStance() == EOperativeStance::Prone && FMath::Abs(Feet.Z - State.HallRoofZ) < 15.f,
				FString::Printf(TEXT("the sniper lies prone on the +3 roof (feet %.0f cm over it)"), Feet.Z - State.HallRoofZ));
			// (b) His height level and range.
			const float EyeOver = Sight->GetEyeAboveGroundCm(*Sniper);
			const int32 Level = Sight->GetHeightLevel(*Sniper);
			const float Multiplier = Sight->GetSightRangeMultiplier(*Sniper);
			Check(State, Level == 3 && FMath::IsNearlyEqual(Multiplier, 1.75f, 0.001f) && FMath::IsNearlyEqual(Sight->GetSightRangeCm(*Sniper), 10500.f, 1.f),
				FString::Printf(TEXT("roof sniper: eye %.0f cm over the street -> level %d, range x%.2f = %.0f m (expected +3, x1.75, 105 m)"), EyeOver, Level,
					Multiplier, Sight->GetSightRangeCm(*Sniper) / 100.f));
			// (a) His cone traced from the perch.
			const UVisionConeComponent* Cone = Sniper->VisionCone;
			Check(State, Cone && Cone->IsConeShown() && Cone->IsOccluded(), TEXT("roof sniper: his occluded cone is shown"));
			if (!Cone || !Cone->IsOccluded())
			{
				return Finish(State, false);
			}
			int32 Elevated = 0;
			int32 Rays = 0;
			PerchedRays(*Sniper, State.HallRoofZ - 30.f, Elevated, Rays);
			Check(State, Rays > 0 && Elevated >= Rays * 9 / 10, FString::Printf(TEXT("roof sniper: his cone looks down from the perch (%d / %d rays on the street 9 m below)"),
				Elevated, Rays));
			// The office's shadow (its west wall 3 m, its far face %.0f cm out): a target right behind it hidden, beyond the shadow seen.
			const float Eye = State.HallRoofZ + SightRules::EyeHeight(EOperativeStance::Prone);
			const float FaceDistance = SniperSpot.X - OfficeFarFaceX;
			const float StandingShadow = VisionConeVisibility::WallShadowLengthCm(Eye, OfficeTopCm, FaceDistance, SightRules::ProfileHeight(EOperativeStance::Standing));
			const float CrouchedShadow = VisionConeVisibility::WallShadowLengthCm(Eye, OfficeTopCm, FaceDistance, SightRules::ProfileHeight(EOperativeStance::Crouching));
			UE_LOG(LogCodexTactics, Display, TEXT("[VisionHeight] office: far face %.0f cm out, 3 m; eye %.0f cm over the street; shadow of the ground %.0f cm, of a standing / crouched man %.0f / %.0f cm"),
				FaceDistance, Eye, VisionConeVisibility::WallShadowLengthCm(Eye, OfficeTopCm, FaceDistance), StandingShadow, CrouchedShadow);
			GroundAt(World, FVector(OfficeFarFaceX - 0.5f * StandingShadow, SniperSpot.Y, 0.f), 0.f, State.ShadowPoint);
			GroundAt(World, FVector(OfficeFarFaceX - CrouchedShadow - 150.f, SniperSpot.Y, 0.f), 0.f, State.BeyondPoint);
			for (const TPair<const TCHAR*, FVector>& Probe : { TPair<const TCHAR*, FVector>(TEXT("behind the office's wall (its shadow)"), State.ShadowPoint),
				TPair<const TCHAR*, FVector>(TEXT("beyond the office's shadow"), State.BeyondPoint) })
			{
				const bool bBehind = Probe.Value == State.ShadowPoint;
				const int32 Drawn = Cone->GetDrawnLevelAt(Probe.Value);
				const int32 Seen = SightLevel(*Sight, *Sniper, Probe.Value);
				const int32 Expected = bBehind ? 0 : 2;
				Check(State, !Probe.Value.IsZero() && Drawn == Expected && Seen == Expected,
					FString::Printf(TEXT("roof sniper: a target %s (%.0f cm out): drawn %d, sight %d (expected %d)"), Probe.Key,
						FVector::Dist2D(Probe.Value, Feet), Drawn, Seen, Expected));
			}
			int32 Points = 0;
			int32 Levels[3] = { 0, 0, 0 };
			FString First;
			const int32 Mismatches = CompareGrid(World, *Sight, *Sniper, 0.f, Points, Levels, First);
			Check(State, Points >= 300 && Levels[0] > 30 && Levels[2] > 30 && Mismatches == 0,
				FString::Printf(TEXT("roof sniper: drawn cone = his sight (CanOperativeSee) on a 1 m grid: %d street points (cut %d, standing only %d, full %d), %d mismatches %s"),
					Points, Levels[0], Levels[1], Levels[2], Mismatches, *First));
			const FVisionConeBuildStats& Stats = UVisionConeComponent::GetBuildStats();
			UE_LOG(LogCodexTactics, Display, TEXT("[VisionHeight] cone builds %lld, rays %lld, traces %lld (%.1f traces / ray), task %.2f ms (%.2f ms / build)"), Stats.Builds,
				Stats.Rays, Stats.Traces, Stats.Rays > 0 ? static_cast<float>(Stats.Traces) / Stats.Rays : 0.f, Stats.TaskSeconds * 1000.0,
				Stats.Builds > 0 ? Stats.TaskSeconds * 1000.0 / Stats.Builds : 0.0);
			// (b) The range from the roof: an open target beyond the 60 m ground range.
			State.bTarget = FindRangeTarget(World, *Sight, *Sniper, State);
			Check(State, State.bTarget, FString::Printf(TEXT("an open street target 62-68 m out with a clear line from the roof (%.0f, %.0f)"), State.Target.X, State.Target.Y));
			if (State.bTarget)
			{
				const FVector Profile = State.Target + FVector(0.f, 0.f, SightRules::ProfileHeight(EOperativeStance::Standing));
				const float Distance = static_cast<float>(FVector::Dist(UTacticalSightSubsystem::EyePoint(*Sniper), Profile));
				Check(State, Distance > UTacticalSightSubsystem::MaxSightCm && Sight->CanOperativeSee(*Sniper, Profile),
					FString::Printf(TEXT("roof sniper: the open target %.1f m out (beyond the 60 m ground range, within 105 m) IS seen"), Distance / 100.f));
				SetHeightRangeCVar(0);
				const bool bFlatSees = Sight->CanOperativeSee(*Sniper, Profile);
				SetHeightRangeCVar(1);
				Check(State, !bFlatSees, TEXT("roof sniper: with Codex.Sight.HeightRange 0 (the old flat range) the same target is not seen"));
				Check(State, State.bGroundSpot, FString::Printf(TEXT("a street spot the same distance from the target with a clear line (%.0f, %.0f)"),
					State.GroundSpot.X, State.GroundSpot.Y));
			}
			// A/B: the 2026-11-01 sampler on the same spot (Codex.VisionCones.ElevatedFix 0) - the designer's report reproduced.
			SetElevatedFixCVar(0);
			RequestFreshCone(State, *Sniper);
			Enter(State, EStage::RoofOldSampler);
			return true;
		}

		case EStage::RoofOldSampler:
		{
			HoldSniper(World, State);
			if (!IsConeFresh(State, *Sniper))
			{
				return true;
			}
			int32 Points = 0;
			int32 Levels[3] = { 0, 0, 0 };
			FString First;
			int32 Covered = 0;
			const int32 Mismatches = CompareGrid(World, *Sight, *Sniper, 0.f, Points, Levels, First, &Covered);
			int32 Elevated = 0;
			int32 Rays = 0;
			PerchedRays(*Sniper, State.HallRoofZ - 30.f, Elevated, Rays);
			const int32 OfficeShadow = Sniper->VisionCone->GetDrawnLevelAt(State.ShadowPoint);
			Check(State, Mismatches > 0 && Covered > 0,
				FString::Printf(TEXT("A/B, the 2026-11-01 sampler (Codex.VisionCones.ElevatedFix 0) on this spot reproduces the report: %d / %d rays look down, %d of %d grid points drawn wrong (%d drawn although hidden), the office's shadow drawn %d"),
					Elevated, Rays, Mismatches, Points, Covered, OfficeShadow));
			SetElevatedFixCVar(1);
			Sniper->VisionCone->RequestRebuild();
			Enter(State, State.bShot ? EStage::ShotFrame : EStage::CutChecks);
			return true;
		}

		case EStage::ShotFrame:
		case EStage::ShotCloseFrame:
		{
			HoldSniper(World, State);
			UVisionConeComponent::SetPlayerSquadCones(false);
			Sniper->bShowVisionCone = true;
			if (State.Stage == EStage::ShotFrame)
			{
				// The sniper on the hall's edge, his cone over the street, the office and its shadow, the ground beyond.
				FocusCamera(World, FVector(OfficeFarFaceX + 600.f, SniperSpot.Y + 300.f, 0.f), 5600.f, SniperYaw - 90.f);
			}
			else
			{
				FocusCamera(World, FVector(OfficeFarFaceX - 500.f, SniperSpot.Y, 0.f), 3200.f, SniperYaw - 90.f);
			}
			if (State.StageTime < 2.5f)
			{
				return true;
			}
			Enter(State, State.Stage == EStage::ShotFrame ? EStage::ShotTake : EStage::ShotCloseTake);
			return true;
		}

		case EStage::ShotTake:
		case EStage::ShotCloseTake:
		{
			HoldSniper(World, State);
			if (!State.bShotRequested)
			{
				State.bShotRequested = true;
				const TCHAR* Name = State.Stage == EStage::ShotTake ? TEXT("roof_sniper_cone.png") : TEXT("roof_sniper_shadow.png");
				const FString Path = FPaths::ProjectSavedDir() / TEXT("Screenshots") / TEXT("VisionHeight") / Name;
				FScreenshotRequest::RequestScreenshot(Path, false, false);
				UE_LOG(LogCodexTactics, Display, TEXT("[VisionHeight] screenshot requested: %s"), *Path);
				return true;
			}
			if (State.StageTime < 1.5f)
			{
				return true;
			}
			State.bShotRequested = false;
			if (State.Stage == EStage::ShotTake)
			{
				Enter(State, EStage::ShotCloseFrame);
				return true;
			}
			Sniper->bShowVisionCone = false;
			UVisionConeComponent::SetPlayerSquadCones(true);
			ReleaseCamera(World);
			Enter(State, EStage::CutChecks);
			return true;
		}

		case EStage::CutChecks:
		{
			HoldSniper(World, State);
			// (b) Below the ground: the operative in the -3 m cut has the ground range, never less.
			if (Mate->GetStance() != EOperativeStance::Standing && State.StageTime < 1.f)
			{
				Mate->SetStance(EOperativeStance::Standing); // the squad copies the prone leader's stance: he stands up for the check
				return true;
			}
			const FVector Feet = FeetOf(*Mate);
			const int32 Level = Sight->GetHeightLevel(*Mate);
			Check(State, FMath::Abs(Feet.Z - State.CutFloorZ) < 15.f && Level == 0 && FMath::IsNearlyEqual(Sight->GetSightRangeCm(*Mate), UTacticalSightSubsystem::MaxSightCm, 1.f),
				FString::Printf(TEXT("in the -3 m cut (feet z %.0f): eye %.0f cm over his floor -> level %d, range %.0f m (expected 0, the ground's 60 m)"), Feet.Z,
					Sight->GetEyeAboveGroundCm(*Mate), Level, Sight->GetSightRangeCm(*Mate) / 100.f));
			bool bDone = false;
			for (const float Yaw : { 90.f, -90.f })
			{
				const FVector Dir = FRotator(0.f, Yaw, 0.f).Vector();
				const FVector Eye = UTacticalSightSubsystem::EyePoint(*Mate);
				const FVector Near = FVector(Feet.X, Feet.Y, State.CutFloorZ) + Dir * 5000.f + FVector(0.f, 0.f, SightRules::ProfileHeight(EOperativeStance::Standing));
				const FVector Far = FVector(Feet.X, Feet.Y, State.CutFloorZ) + Dir * 7000.f + FVector(0.f, 0.f, SightRules::ProfileHeight(EOperativeStance::Standing));
				if (!Sight->HasClearSight(Eye, Near) || !Sight->HasClearSight(Eye, Far))
				{
					continue;
				}
				Mate->SetActorRotation(FRotator(0.f, Yaw, 0.f));
				Check(State, Sight->CanOperativeSee(*Mate, Near), FString::Printf(TEXT("in the cut, facing %.0f: a target 50 m along the cut IS seen"), Yaw));
				Check(State, !Sight->CanOperativeSee(*Mate, Far),
					FString::Printf(TEXT("in the cut, facing %.0f: a target 70 m along it with a clear line is NOT seen (the ground range, no bonus below ground)"), Yaw));
				bDone = true;
				break;
			}
			Check(State, bDone, TEXT("in the cut: a clear line 70 m along it"));
			Enter(State, EStage::GroundRange);
			return true;
		}

		case EStage::GroundRange:
		{
			HoldSniper(World, State);
			if (!State.bGroundSpot)
			{
				Enter(State, EStage::RuinSettle);
				return true;
			}
			// (b) The same target at the same distance from ground level: beyond the 60 m ground range - not seen.
			if (State.StageTime < KINDA_SMALL_NUMBER + Dt)
			{
				Place(*Mate, State.GroundSpot, YawTo(State.GroundSpot, State.Target));
				Mate->SetStance(EOperativeStance::Standing);
				return true;
			}
			Mate->SetActorRotation(FRotator(0.f, YawTo(State.GroundSpot, State.Target), 0.f));
			if (Mate->GetStance() != EOperativeStance::Standing && State.StageTime < 1.f)
			{
				Mate->SetStance(EOperativeStance::Standing); // the squad copies the prone leader's stance: he stands up for the check
				return true;
			}
			const FVector Profile = State.Target + FVector(0.f, 0.f, SightRules::ProfileHeight(EOperativeStance::Standing));
			const FVector Eye = UTacticalSightSubsystem::EyePoint(*Mate);
			const float Distance = static_cast<float>(FVector::Dist(Eye, Profile));
			UE_LOG(LogCodexTactics, Display, TEXT("[VisionHeight] ground spot (%.0f, %.0f, %.0f) eye (%.0f, %.0f, %.0f) -> target (%.0f, %.0f, %.0f): blocker %s"),
				State.GroundSpot.X, State.GroundSpot.Y, State.GroundSpot.Z, Eye.X, Eye.Y, Eye.Z, Profile.X, Profile.Y, Profile.Z, *Blocker(World, Eye, Profile));
			Check(State, Sight->GetHeightLevel(*Mate) == 0 && Sight->HasClearSight(Eye, Profile) && !Sight->CanOperativeSee(*Mate, Profile),
				FString::Printf(TEXT("on the ground (level %d, feet %.0f cm off the spot, cover %d) %.1f m from the same target, clear line %s: NOT seen (beyond the 60 m ground range)"),
					Sight->GetHeightLevel(*Mate), FVector::Dist(FeetOf(*Mate), State.GroundSpot), Mate->bInCover ? 1 : 0, Distance / 100.f,
					Sight->HasClearSight(Eye, Profile) ? TEXT("yes") : TEXT("no")));
			Enter(State, EStage::RuinSettle);
			return true;
		}

		case EStage::RuinSettle:
		{
			HoldSniper(World, State);
			if (State.StageTime < KINDA_SMALL_NUMBER + Dt)
			{
				Mate->SetStance(EOperativeStance::Prone);
				ConeOn(State, *Mate, RuinConeCm);
				return true;
			}
			const FVector Feet = FeetOf(*Mate);
			if (FVector::Dist2D(Feet, FVector(RuinSpot, 0.f)) > 10.f || FMath::Abs(Feet.Z - State.RuinRoofZ) > 10.f)
			{
				Place(*Mate, FVector(RuinSpot, State.RuinRoofZ), RuinYaw);
			}
			Mate->SetActorRotation(FRotator(0.f, RuinYaw, 0.f));
			if (State.StageTime < 2.5f)
			{
				return true;
			}
			RequestFreshCone(State, *Mate);
			Enter(State, EStage::RuinChecks);
			return true;
		}

		case EStage::RuinChecks:
		{
			HoldSniper(World, State);
			if (FVector::Dist2D(FeetOf(*Mate), FVector(RuinSpot, 0.f)) > 10.f || FMath::Abs(FeetOf(*Mate).Z - State.RuinRoofZ) > 10.f)
			{
				Place(*Mate, FVector(RuinSpot, State.RuinRoofZ), RuinYaw);
			}
			Mate->SetActorRotation(FRotator(0.f, RuinYaw, 0.f));
			if (!IsConeFresh(State, *Mate))
			{
				return true;
			}
			Mate->SetActorRotation(FRotator(0.f, RuinYaw, 0.f));
			const FVector Feet = FeetOf(*Mate);
			const int32 Level = Sight->GetHeightLevel(*Mate);
			Check(State, !Mate->bInCover, TEXT("ruin roof: he lies in the open (not in cover: he looks where the scene faces him)"));
			UE_LOG(LogCodexTactics, Display, TEXT("[VisionHeight] ruin: feet (%.0f, %.0f, %.0f), at the rebuild (%.0f, %.0f), cone at (%.0f, %.0f); actor yaw %.1f, sight facing yaw %.1f, cone yaw %.1f, view override %d, stance %d, rebuild requests %d"),
				Feet.X, Feet.Y, Feet.Z, State.RebuildFeet.X, State.RebuildFeet.Y, Mate->VisionCone->GetComponentLocation().X, Mate->VisionCone->GetComponentLocation().Y,
				Mate->GetActorRotation().Yaw, Mate->GetSightFacing().Rotation().Yaw, Mate->VisionCone->GetComponentRotation().Yaw,
				Mate->VisionCone->HasViewOverride() ? 1 : 0, static_cast<int32>(Mate->GetStance()), State.Rebuilds);
			Check(State, FMath::Abs(Feet.Z - State.RuinRoofZ) < 15.f && Level == 2 && FMath::IsNearlyEqual(Sight->GetSightRangeMultiplier(*Mate), 1.5f, 0.001f),
				FString::Printf(TEXT("ruin roof (prone over its interior wall's top): eye %.0f cm over the street -> level %d, x%.2f (expected +2, x1.5)"),
					Sight->GetEyeAboveGroundCm(*Mate), Level, Sight->GetSightRangeMultiplier(*Mate)));
			int32 Elevated = 0;
			int32 Rays = 0;
			PerchedRays(*Mate, 150.f, Elevated, Rays);
			Check(State, Mate->VisionCone && Mate->VisionCone->IsOccluded() && Rays > 0 && Elevated >= Rays / 2,
				FString::Printf(TEXT("ruin roof: his cone traced from the perch (%d / %d rays look down; the wall top under him is the roof's support, not the ground)"),
					Elevated, Rays));
			if (Mate->VisionCone && Mate->VisionCone->IsOccluded())
			{
				int32 Points = 0;
				int32 Levels[3] = { 0, 0, 0 };
				FString First;
				const int32 Mismatches = CompareGrid(World, *Sight, *Mate, 0.f, Points, Levels, First);
				Check(State, Points >= 150 && Levels[0] > 20 && Levels[2] > 20 && Mismatches == 0,
					FString::Printf(TEXT("ruin roof: drawn cone = his sight on a 1 m grid: %d street points (cut %d, standing only %d, full %d), %d mismatches %s"),
						Points, Levels[0], Levels[1], Levels[2], Mismatches, *First));
			}
			Enter(State, EStage::Enemies);
			return true;
		}

		case EStage::Enemies:
		{
			// (c) One model for the armed humans: a Marksman on the hall's roof where the sniper lay.
			UWaveSubsystem* Waves = World->GetSubsystem<UWaveSubsystem>();
			HideFromRoof(World, *Sniper, 250.f);
			Sniper->SetStance(EOperativeStance::Standing);
			Mate->SetStance(EOperativeStance::Standing);
			const FVector RoofFeet(SniperSpot, State.HallRoofZ);
			AEnemyCharacter* Marksman = Waves ? Waves->SpawnLevelEnemy(EEnemyArchetype::Marksman, RoofFeet + FVector(0.f, 0.f, 110.f), FRotator(0.f, SniperYaw, 0.f)) : nullptr;
			AEnemyCharacter* Hound = Waves ? Waves->SpawnLevelEnemy(EEnemyArchetype::FrostHound, RoofFeet + FVector(0.f, -400.f, 80.f), FRotator(0.f, SniperYaw, 0.f)) : nullptr;
			State.Spawned.Add(Marksman);
			State.Spawned.Add(Hound);
			Check(State, Marksman != nullptr && Hound != nullptr, TEXT("a Marksman and a frost hound spawned on the hall's roof"));
			if (!Marksman || !Hound || !State.bTarget)
			{
				Check(State, State.bTarget, TEXT("the open target found (the roof scene)"));
				return Finish(State, false);
			}
			PlaceEnemy(*Marksman, RoofFeet, SniperYaw);
			PlaceEnemy(*Hound, RoofFeet + FVector(0.f, -400.f, 0.f), SniperYaw);
			const EEnemyArchetype MarksmanType = Marksman->GetArchetype();
			Check(State, MarksmanType == EEnemyArchetype::Marksman && Marksman->UsesHumanSight() && !Hound->UsesHumanSight(),
				TEXT("the Marksman uses the squad's vision model (an armed human), the hound keeps its own senses"));
			const float MarksmanMultiplier = Marksman->GetSightHeightMultiplier();
			const float HoundMultiplier = Hound->GetSightHeightMultiplier();
			Check(State, FMath::IsNearlyEqual(MarksmanMultiplier, 1.75f, 0.001f) && FMath::IsNearlyEqual(HoundMultiplier, 1.f),
				FString::Printf(TEXT("on the +3 roof: the Marksman's sight x%.2f (level %d), the hound's x%.2f (a beast: no bonus)"), MarksmanMultiplier,
					Sight->GetHeightLevel(*Marksman), HoundMultiplier));
			if (UClass* PmcClass = LoadClass<AEnemyCharacter>(nullptr, TEXT("/Game/Characters/Enemies/PMC/FrozenPMC_Fighter/BP_Enemy_FrozenPMC_Fighter.BP_Enemy_FrozenPMC_Fighter_C")))
			{
				Check(State, PmcClass->GetDefaultObject<AEnemyCharacter>() && PmcClass->GetDefaultObject<AEnemyCharacter>()->UsesHumanSight(),
					TEXT("the PMC fighter class (BP_Enemy_FrozenPMC_Fighter) uses the squad's vision model"));
			}
			Hound->Destroy();
			// The squad member in the open at the elevated range (beyond the Marksman's 45 m ground range, within 78.75 m).
			const FEnemyPerceptionParams Params = EnemyPerception::Get(EEnemyArchetype::Marksman);
			Place(*Mate, State.Target, YawTo(State.Target, RoofFeet));
			Marksman->SetActorRotation(FRotator(0.f, YawTo(RoofFeet, State.Target), 0.f));
			float Distance = 0.f;
			float Range = 0.f;
			const AActor* Seen = Marksman->FindVisibleOperativeNow(Distance, Range);
			Check(State, Seen == Mate && Distance > Params.SightRangeCm && Distance <= Params.SightRangeCm * 1.75f + 1.f,
				FString::Printf(TEXT("a Marksman on the +3 roof spots an operative in the open %.1f m out (his ground range %.0f m, elevated %.1f m): %s"),
					FVector::Dist(Marksman->GetActorLocation(), Mate->GetActorLocation()) / 100.f, Params.SightRangeCm / 100.f, Range / 100.f,
					Seen ? *Seen->GetName() : TEXT("nobody")));
			// Not through walls: the operative right behind the office (its shadow).
			Place(*Mate, State.ShadowPoint, 0.f);
			Marksman->SetActorRotation(FRotator(0.f, YawTo(RoofFeet, State.ShadowPoint), 0.f));
			Seen = Marksman->FindVisibleOperativeNow(Distance, Range);
			Check(State, Seen == nullptr && !Sight->HasClearSight(UTacticalSightSubsystem::EyePoint(*Marksman), UTacticalSightSubsystem::ProfilePoint(*Mate)),
				FString::Printf(TEXT("the Marksman on the roof does NOT spot an operative behind the office's wall (%.1f m out): %s"),
					FVector::Dist2D(RoofFeet, State.ShadowPoint) / 100.f, Seen ? *Seen->GetName() : TEXT("nobody")));
			// The same operative at the same distance from the ground: beyond the Marksman's 45 m.
			if (State.bGroundSpot)
			{
				Place(*Mate, State.Target, 0.f);
				PlaceEnemy(*Marksman, State.GroundSpot, YawTo(State.GroundSpot, State.Target));
				Seen = Marksman->FindVisibleOperativeNow(Distance, Range);
				const bool bClear = Sight->HasClearSight(UTacticalSightSubsystem::EyePoint(*Marksman), UTacticalSightSubsystem::ProfilePoint(*Mate));
				Check(State, Seen == nullptr && bClear && FMath::IsNearlyEqual(Marksman->GetSightHeightMultiplier(), 1.f),
					FString::Printf(TEXT("the same Marksman on the ground %.1f m from that operative, clear line %s: does NOT spot him (x%.2f, %.0f m)"),
						FVector::Dist(Marksman->GetActorLocation(), Mate->GetActorLocation()) / 100.f, bClear ? TEXT("yes") : TEXT("no"),
						Marksman->GetSightHeightMultiplier(), Params.SightRangeCm / 100.f));
			}
			Marksman->Destroy();
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
		TEXT("CodexTactics.VisionHeightSmoke"),
		TEXT("Dev check (game designer 2026-11-02, L_Gen_Depot_1): a prone sniper on a +3 roof - his cone cuts the walls' shadows and equals his sight, ")
		TEXT("the sight range grows with the height level (+3 x1.75), the -3 m cut keeps the ground range, a Marksman on the roof shares the model. ")
		TEXT("Arg Shot saves Saved/Screenshots/VisionHeight/*.png. PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
