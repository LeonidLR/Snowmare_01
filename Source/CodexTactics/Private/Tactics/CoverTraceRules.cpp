#include "Tactics/CoverTraceRules.h"

#include "Combat/EnemyGhostActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "NavigationSystem.h"

namespace CoverTraceRules
{
	ECoverHeight ClassifyHeight(float WallTopCm, const FCoverTraceConfig& Config)
	{
		if (WallTopCm < Config.MinCoverHeightCm)
		{
			return ECoverHeight::None;
		}
		if (WallTopCm >= Config.HighCoverMinCm)
		{
			return ECoverHeight::HighCover;
		}
		// Below LowCoverMaxCm, and the 130-180 band too (hides a crouched man, not a standing one): low cover.
		return ECoverHeight::LowCover;
	}

	ECoverHeight ClassifyFromTraces(bool bChestHit, bool bHeadHit, bool bKneeHit)
	{
		if (bChestHit && bHeadHit)
		{
			return ECoverHeight::HighCover;
		}
		return bChestHit || bKneeHit ? ECoverHeight::LowCover : ECoverHeight::None;
	}

	FVector ComputeSlotLocation(const FVector& WallPoint, const FVector& WallNormal, float OffsetCm, float GroundZ)
	{
		const FVector Normal = FVector(WallNormal.X, WallNormal.Y, 0.f).GetSafeNormal();
		const FVector Slot = WallPoint + Normal * OffsetCm;
		return FVector(Slot.X, Slot.Y, GroundZ);
	}

	bool EdgeExposedFromProbes(const TArray<bool>& ProbeHits, float StepCm, float& OutEdgeDistanceCm)
	{
		OutEdgeDistanceCm = 0.f;
		for (int32 Index = 0; Index < ProbeHits.Num(); ++Index)
		{
			if (!ProbeHits[Index])
			{
				OutEdgeDistanceCm = (Index + 1) * StepCm;
				return true;
			}
		}
		return false;
	}

	bool IsSameWall(const FCoverSlot& A, const FCoverSlot& B, const FCoverTraceConfig& Config)
	{
		if (!A.IsValid() || !B.IsValid())
		{
			return false;
		}
		if (FVector::DotProduct(A.WallNormal, B.WallNormal) < Config.SameWallNormalDot)
		{
			return false;
		}
		const float PlaneDistance = FMath::Abs(FVector::DotProduct(B.WallPoint - A.WallPoint, A.WallNormal));
		return PlaneDistance <= Config.SameWallPlaneDistanceCm;
	}

	float AlongWallDistance(const FCoverSlot& A, const FVector& Point)
	{
		return static_cast<float>(FVector::DotProduct(Point - A.WorldLocation, A.RightTangent()));
	}

	ECoverFacing ChooseFacing(const FCoverSlot& Slot, const FVector* ThreatLocation)
	{
		if (Slot.bLeftEdgeExposed != Slot.bRightEdgeExposed)
		{
			return Slot.bLeftEdgeExposed ? ECoverFacing::Left : ECoverFacing::Right;
		}
		if (ThreatLocation)
		{
			return AlongWallDistance(Slot, *ThreatLocation) < 0.f ? ECoverFacing::Left : ECoverFacing::Right;
		}
		return ECoverFacing::Right;
	}

	float FacingYaw(const FCoverSlot& Slot)
	{
		return Slot.WallNormal.Rotation().Yaw;
	}

	namespace
	{
		bool CoverTrace(UWorld* World, const FVector& From, const FVector& To, const TArray<const AActor*>& Ignored, FHitResult& OutHit)
		{
			FCollisionQueryParams Params(SCENE_QUERY_STAT(CoverTrace), false);
			for (const AActor* Actor : Ignored)
			{
				Params.AddIgnoredActor(Actor);
			}
			for (TActorIterator<APawn> It(World); It; ++It)
			{
				Params.AddIgnoredActor(*It); // bodies are no walls
			}
			for (TActorIterator<AEnemyGhostActor> It(World); It; ++It)
			{
				Params.AddIgnoredActor(*It); // silhouettes neither
			}
			return World->LineTraceSingleByChannel(OutHit, From, To, ECC_Visibility, Params);
		}

		bool FindGround(UWorld* World, const FVector& Around, const TArray<const AActor*>& Ignored, float& OutGroundZ)
		{
			FHitResult Hit;
			if (CoverTrace(World, Around + FVector(0.f, 0.f, 250.f), Around - FVector(0.f, 0.f, 300.f), Ignored, Hit))
			{
				OutGroundZ = Hit.ImpactPoint.Z;
				return true;
			}
			return false;
		}
	}

	bool FindCoverSlotAt(UWorld* World, const FVector& CursorHitPoint, const FVector& TraceDirection, FCoverSlot& OutSlot,
		const FCoverTraceConfig& Config, const TArray<const AActor*>& IgnoredActors)
	{
		OutSlot = FCoverSlot();
		if (!World)
		{
			return false;
		}
		FVector Direction = FVector(TraceDirection.X, TraceDirection.Y, 0.f).GetSafeNormal();
		if (Direction.IsNearlyZero())
		{
			return false;
		}
		// The ground in front of the wall (80 cm short of the click, where the operative would stand).
		const FVector Start2D = CursorHitPoint - Direction * 80.f;
		float GroundZ = 0.f;
		if (!FindGround(World, FVector(Start2D.X, Start2D.Y, CursorHitPoint.Z), IgnoredActors, GroundZ))
		{
			GroundZ = CursorHitPoint.Z - Config.ChestHeightCm; // a click high on a wall: assume the chest
		}
		// Chest trace towards the wall; a 60 cm barricade is below the chest, so the knee trace finds those.
		const float Reach = 80.f + Config.MaxWallDistanceCm;
		const FVector ChestStart(Start2D.X, Start2D.Y, GroundZ + Config.ChestHeightCm);
		FHitResult Chest;
		const bool bChestHit = CoverTrace(World, ChestStart, ChestStart + Direction * Reach, IgnoredActors, Chest)
			&& FMath::Abs(Chest.ImpactNormal.Z) <= Config.MaxWallNormalZ;
		const FVector KneeStart(Start2D.X, Start2D.Y, GroundZ + Config.KneeHeightCm);
		FHitResult Knee;
		const bool bKneeHit = CoverTrace(World, KneeStart, KneeStart + Direction * Reach, IgnoredActors, Knee)
			&& FMath::Abs(Knee.ImpactNormal.Z) <= Config.MaxWallNormalZ;
		if (!bChestHit && !bKneeHit)
		{
			return false;
		}
		if (!bChestHit)
		{
			Chest = Knee; // the low obstacle's surface
		}
		FVector Normal = FVector(Chest.ImpactNormal.X, Chest.ImpactNormal.Y, 0.f);
		if (FMath::Abs(Chest.ImpactNormal.Z) > Config.MaxWallNormalZ || !Normal.Normalize())
		{
			return false; // a floor / ramp, not a wall
		}
		if (FVector::DotProduct(Normal, Direction) > 0.f)
		{
			Normal = -Normal; // always facing the clicker's side
		}
		// Head trace at the same spot.
		const FVector HeadStart(Start2D.X, Start2D.Y, GroundZ + Config.HeadHeightCm);
		FHitResult Head;
		const bool bHeadHit = CoverTrace(World, HeadStart, HeadStart + Direction * (80.f + Config.MaxWallDistanceCm), IgnoredActors, Head)
			&& FMath::Abs(Head.ImpactNormal.Z) <= Config.MaxWallNormalZ;
		// Wall top: straight down onto the wall just inside its surface.
		const FVector Inside = Chest.ImpactPoint - Normal * 10.f;
		FHitResult Top;
		float WallTopCm = 0.f;
		if (CoverTrace(World, FVector(Inside.X, Inside.Y, GroundZ + 450.f), FVector(Inside.X, Inside.Y, GroundZ + 5.f), IgnoredActors, Top))
		{
			WallTopCm = Top.ImpactPoint.Z - GroundZ;
		}
		ECoverHeight Height = WallTopCm > 0.f ? ClassifyHeight(WallTopCm, Config) : ClassifyFromTraces(bChestHit, bHeadHit, bKneeHit);
		if (Height == ECoverHeight::None)
		{
			return false;
		}
		if (WallTopCm <= 0.f)
		{
			WallTopCm = bHeadHit ? Config.HighCoverMinCm : Config.ChestHeightCm;
		}

		OutSlot.WallPoint = Chest.ImpactPoint;
		OutSlot.WallNormal = Normal;
		OutSlot.Height = Height;
		OutSlot.WallTopCm = WallTopCm;
		OutSlot.WallActor = Chest.GetActor();
		// Feet: offset off the wall, on the navmesh (the agent radius already keeps the navmesh ~40 cm off walls).
		const FVector Raw = ComputeSlotLocation(Chest.ImpactPoint, Normal, Config.SlotOffsetCm, GroundZ);
		OutSlot.WorldLocation = Raw;
		if (UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World))
		{
			FNavLocation OnNav;
			if (Nav->ProjectPointToNavigation(Raw, OnNav, Config.NavProjectExtent))
			{
				// Keep the wall distance: the projection may pull the point along the normal; clamp it to 35..90 cm off the wall.
				const float Off = FVector::DotProduct(OnNav.Location - Chest.ImpactPoint, Normal);
				const FVector Fixed = OnNav.Location + Normal * (FMath::Clamp(Off, Config.SlotOffsetCm - 10.f, Config.SlotOffsetCm * 2.f) - Off);
				OutSlot.WorldLocation = FVector(Fixed.X, Fixed.Y, OnNav.Location.Z);
			}
		}
		// Corners: probes along the wall, from 60 cm off the surface back into it.
		const FVector Right = OutSlot.RightTangent();
		for (int32 Side = 0; Side < 2; ++Side)
		{
			const FVector Along = Side == 0 ? -Right : Right;
			TArray<bool> Hits;
			for (int32 Probe = 1; Probe <= Config.MaxEdgeProbes; ++Probe)
			{
				const FVector ProbeStart = FVector(Chest.ImpactPoint.X, Chest.ImpactPoint.Y, GroundZ + Config.ChestHeightCm) + Normal * 60.f
					+ Along * (Probe * Config.EdgeProbeStepCm);
				FHitResult ProbeHit;
				const bool bHit = CoverTrace(World, ProbeStart, ProbeStart - Normal * 140.f, IgnoredActors, ProbeHit)
					&& FMath::Abs(ProbeHit.ImpactNormal.Z) <= Config.MaxWallNormalZ;
				Hits.Add(bHit);
			}
			float EdgeDistance = 0.f;
			const bool bExposed = EdgeExposedFromProbes(Hits, Config.EdgeProbeStepCm, EdgeDistance);
			if (Side == 0)
			{
				OutSlot.bLeftEdgeExposed = bExposed;
				OutSlot.LeftEdgeDistanceCm = EdgeDistance;
			}
			else
			{
				OutSlot.bRightEdgeExposed = bExposed;
				OutSlot.RightEdgeDistanceCm = EdgeDistance;
			}
		}
		return true;
	}

	bool FindShimmySlot(UWorld* World, const FCoverSlot& Current, const FVector& ClickPoint, FCoverSlot& OutSlot, const FCoverTraceConfig& Config,
		const TArray<const AActor*>& IgnoredActors)
	{
		OutSlot = FCoverSlot();
		if (!World || !Current.IsValid())
		{
			return false;
		}
		// The click projected onto the wall line, then traced from the open side towards the wall.
		const float Along = AlongWallDistance(Current, ClickPoint);
		const FVector OnWall = Current.WallPoint + Current.RightTangent() * Along;
		const FVector Probe = OnWall + Current.WallNormal * 60.f;
		if (!FindCoverSlotAt(World, FVector(Probe.X, Probe.Y, Current.WallPoint.Z), -Current.WallNormal, OutSlot, Config, IgnoredActors))
		{
			// Short probe from the open side: start 80 cm short of the point, so use the open-side point itself as the "click".
			return false;
		}
		return IsSameWall(Current, OutSlot, Config);
	}
}
