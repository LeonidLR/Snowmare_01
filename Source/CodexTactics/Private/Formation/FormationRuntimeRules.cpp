#include "Formation/FormationRuntimeRules.h"

#include "Formation/FormationTerrainRules.h"
#include "HAL/IConsoleManager.h"
#include "Templates/Function.h"

namespace FormationRuntime
{
	static TAutoConsoleVariable<int32> CVarFormationSectors(TEXT("Codex.Formation.Sectors"), 1,
		TEXT("1 (default): formation look sectors (docs/design/FORMATION_SECTORS.md): diamond / wedge / pair, column, echelon and the wall layout by terrain, ")
		TEXT("each member looks into his sector (patrol scans, rear man backwards), the squad patrols at pace.patrol, the follower speed leads with the slot's ")
		TEXT("velocity, a group cover order in group mode = the wall layout (crouched, facing out); 0: the old triangle formation, facing and speeds."));

	bool IsEnabled()
	{
		return CVarFormationSectors.GetValueOnGameThread() != 0;
	}

	TOptional<EFormationRole> RoleForFollowerSlot(int32 SlotIndex, int32 MemberCount)
	{
		if (SlotIndex < 0 || MemberCount < 2)
		{
			return TOptional<EFormationRole>();
		}
		if (MemberCount == 2)
		{
			return SlotIndex == 0 ? TOptional<EFormationRole>(EFormationRole::Rear) : TOptional<EFormationRole>();
		}
		// Rules v2.2 (user decision 2026-10-25): three = K, P, Z - the third man is the rear guard, there is no L.
		if (MemberCount == 3)
		{
			return SlotIndex == 0 ? TOptional<EFormationRole>(EFormationRole::Rear)
				: (SlotIndex == 1 ? TOptional<EFormationRole>(EFormationRole::Right) : TOptional<EFormationRole>());
		}
		switch (SlotIndex)
		{
		case 0: return EFormationRole::Left;
		case 1: return EFormationRole::Right;
		case 2: return EFormationRole::Rear;
		default: return TOptional<EFormationRole>();
		}
	}

	EFormationMode ResolveMode(bool bLeaderMoving, bool bLeaderSprinting, bool bWallLayout)
	{
		if (bWallLayout)
		{
			return EFormationMode::Wall;
		}
		if (!bLeaderMoving)
		{
			return EFormationMode::Halt;
		}
		return bLeaderSprinting ? EFormationMode::Run : EFormationMode::Walk;
	}

	EFormationKind ChooseFormation(EFormationTerrainClass Class, int32 MemberCount, bool bLeaderStopped, EFormationKind Previous, bool bBlizzard)
	{
		const EFormationKind Wanted = FormationTerrainRules::FormationFor(Class, MemberCount, bBlizzard);
		if (Wanted == EFormationKind::Wall && !bLeaderStopped)
		{
			return Previous == EFormationKind::Wall ? FormationTerrainRules::FormationFor(EFormationTerrainClass::Open, MemberCount, bBlizzard) : Previous;
		}
		return Wanted;
	}

	bool AcceptProbe(EFormationTerrainClass Raw, const TOptional<EFormationTerrainClass>& PreviousProbeRaw, const TOptional<EFormationTerrainClass>& InForce)
	{
		return !InForce.IsSet() || Raw == InForce.GetValue() || (PreviousProbeRaw.IsSet() && PreviousProbeRaw.GetValue() == Raw);
	}

	TArray<FProbeRay> ProbeRays(float HeadingYaw, const FFormationConfig& Config)
	{
		TArray<FProbeRay> Rays;
		const auto Add = [&Rays](float Yaw, float Length)
		{
			FProbeRay Ray;
			Ray.Yaw = FRotator::NormalizeAxis(Yaw);
			Ray.Length = Length;
			Rays.Add(Ray);
		};
		Add(HeadingYaw, FMath::Max(Config.ClassifyAtWallDist, WallAheadCm));
		Add(HeadingYaw - 20.f, Config.ClassifyAtWallDist);
		Add(HeadingYaw + 20.f, Config.ClassifyAtWallDist);
		for (const float Side : { 1.f, -1.f })
		{
			for (const float Deg : { 60.f, 90.f, 120.f })
			{
				const float Sine = FMath::Sin(FMath::DegreesToRadians(Deg));
				Add(HeadingYaw + Side * Deg, Config.ClassifyProbeSide / Sine);
			}
		}
		return Rays;
	}

	FFormationWall WallFromHit(const FVector2D& Point, const FVector2D& Normal, float HalfLength)
	{
		const FVector2D N = Normal.GetSafeNormal();
		const FVector2D Along = N.IsNearlyZero() ? FVector2D(1.0, 0.0) : FVector2D(-N.Y, N.X);
		FFormationWall Wall;
		Wall.A = Point - Along * HalfLength;
		Wall.B = Point + Along * HalfLength;
		Wall.Thickness = 0.f;
		return Wall;
	}

	FFormationWall WallAlong(const FVector2D& FacePoint, float NormalYaw, float RoomLeft, float RoomRight)
	{
		// FormationWallRules: "right" = the right hand of a man FACING the wall = yaw NormalYaw + 270.
		const FVector2D RightAlong = FormationMath::Forward(NormalYaw + 270.f);
		FFormationWall Wall;
		Wall.A = FacePoint - RightAlong * FMath::Max(RoomLeft, 0.f);
		Wall.B = FacePoint + RightAlong * FMath::Max(RoomRight, 0.f);
		Wall.Thickness = 0.f;
		return Wall;
	}

	float MeasureRoom(const TArray<bool>& Samples, float StepCm)
	{
		int32 Continuous = 0;
		for (const bool bWall : Samples)
		{
			if (!bWall)
			{
				break;
			}
			++Continuous;
		}
		return static_cast<float>(Continuous) * StepCm;
	}

	FFollowerMove FollowerMove(const FVector2D& MemberPos, float FacingYaw, const FVector2D& SlotPos, const FVector2D& SlotVelocity, float Jitter,
		float TopSpeed, float WalkSpeed, bool bRun, const FFormationConfig& Config)
	{
		FFollowerMove Move;
		const FVector2D ToSlot = SlotPos - MemberPos;
		const double Distance = ToSlot.Size();
		if (Distance < StopRadiusCm && SlotVelocity.Size() < SlotStillSpeed)
		{
			Move.bStop = true;
			return Move;
		}
		const double Top = FMath::Max(TopSpeed, 0.f) * Config.MoveCatchupMult;
		FVector2D FeedForward = SlotVelocity;
		if (FeedForward.Size() > Top && FeedForward.Size() > 0.0)
		{
			FeedForward *= Top / FeedForward.Size();
		}
		const FVector2D Dir = Distance > 1e-6 ? ToSlot / Distance : FVector2D::ZeroVector;
		FVector2D Velocity = FeedForward * Jitter + Dir * FMath::Min(Top, Distance * FollowGain);
		double Speed = Velocity.Size();
		if (Speed < 1e-6)
		{
			Move.bStop = true;
			return Move;
		}
		double Cap = Top;
		Move.bBackward = FormationMath::AngleBetween(FormationMath::YawOf(Velocity), FacingYaw) > BackwardAngleDeg;
		if (!bRun && Move.bBackward)
		{
			Cap = FMath::Min(Cap, static_cast<double>(WalkSpeed) * Config.MoveBackwardMult);
		}
		if (Speed > Cap)
		{
			Velocity *= Cap / Speed;
			Speed = Cap;
		}
		Move.Velocity = Velocity;
		Move.Speed = static_cast<float>(Speed);
		return Move;
	}

	FVector2D LeadDestination(const FVector2D& SlotPos, const FVector2D& SlotVelocity, float Lead)
	{
		if (SlotVelocity.Size() < SlotStillSpeed)
		{
			return SlotPos;
		}
		const FVector2D Ahead = SlotVelocity * Lead;
		return SlotPos + (Ahead.Size() > LeadMaxCm ? Ahead.GetSafeNormal() * LeadMaxCm : Ahead);
	}

	bool UseWallLayoutYaw(float DistanceToSlotCm, bool bMoving)
	{
		return !bMoving || DistanceToSlotCm <= WallTurnDistCm;
	}

	TArray<int32> AssignByTravel(const TArray<FVector2D>& From, const TArray<FVector2D>& To)
	{
		TArray<int32> Best;
		if (From.Num() > To.Num() || To.Num() > 4)
		{
			return Best;
		}
		// Every injective choice of places (at most 4 x 3 x 2 x 1 = 24), member i first; the places are tried in index order, so an
		// equal sum keeps the earlier (member-order) assignment.
		double BestCost = TNumericLimits<double>::Max();
		TArray<int32> Current;
		TArray<bool> Used;
		Used.Init(false, To.Num());
		TFunction<void(double)> Visit = [&](double CostSoFar)
		{
			const int32 Member = Current.Num();
			if (Member == From.Num())
			{
				if (CostSoFar < BestCost - 1e-3)
				{
					BestCost = CostSoFar;
					Best = Current;
				}
				return;
			}
			for (int32 Place = 0; Place < To.Num(); ++Place)
			{
				if (!Used[Place])
				{
					Used[Place] = true;
					Current.Add(Place);
					Visit(CostSoFar + FVector2D::Distance(From[Member], To[Place]));
					Current.Pop();
					Used[Place] = false;
				}
			}
		};
		Visit(0.0);
		return Best;
	}

	TOptional<float> HeadingFromVelocity(const FVector2D& Velocity, float MinSpeed)
	{
		return Velocity.Size() > MinSpeed ? TOptional<float>(FormationMath::YawOf(Velocity)) : TOptional<float>();
	}
}
