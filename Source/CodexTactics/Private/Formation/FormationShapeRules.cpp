#include "Formation/FormationShapeRules.h"

#include "Formation/FormationSectorRules.h"

namespace FormationShapeRules
{
	TArray<EFormationRole> Roles(int32 MemberCount)
	{
		if (MemberCount <= 1)
		{
			return { EFormationRole::Commander };
		}
		if (MemberCount == 2)
		{
			return { EFormationRole::Commander, EFormationRole::Rear };
		}
		if (MemberCount == 3)
		{
			// v2.2 (user): the triad has a rear man walking backwards like the four; P sweeps both flanks.
			return { EFormationRole::Commander, EFormationRole::Right, EFormationRole::Rear };
		}
		return { EFormationRole::Commander, EFormationRole::Right, EFormationRole::Left, EFormationRole::Rear };
	}

	int32 EchelonIndex(EFormationKind Formation, EFormationRole Role, int32 MemberCount)
	{
		TArray<EFormationRole> Order;
		if (MemberCount == 2)
		{
			Order = { EFormationRole::Commander, EFormationRole::Rear };
		}
		else if (MemberCount == 3)
		{
			// v2.2: three = K, P, Z both ways (Z last, the rear man; who stands where is FormationAssignRules' minimal travel).
			Order = { EFormationRole::Commander, EFormationRole::Right, EFormationRole::Rear };
		}
		else if (Formation == EFormationKind::EchelonRight)
		{
			Order = { EFormationRole::Commander, EFormationRole::Left, EFormationRole::Right, EFormationRole::Rear };
		}
		else
		{
			Order = { EFormationRole::Commander, EFormationRole::Right, EFormationRole::Left, EFormationRole::Rear };
		}
		return FMath::Max(0, Order.IndexOfByKey(Role));
	}

	FVector2D LocalSlot(EFormationKind Formation, EFormationRole Role, int32 MemberCount, const FFormationConfig& Config)
	{
		if (Role == EFormationRole::Commander)
		{
			return FVector2D::ZeroVector;
		}
		const bool bPair = MemberCount == 2;
		switch (Formation)
		{
		case EFormationKind::Diamond:
		case EFormationKind::Wedge:
			if (bPair)
			{
				return FVector2D(-Config.PairBackOffset, Config.PairSideOffset);
			}
			if (Role == EFormationRole::Right)
			{
				return FVector2D(-Config.SlotBackOffset, Config.SlotSideOffset);
			}
			if (Role == EFormationRole::Left)
			{
				return FVector2D(-Config.SlotBackOffset, -Config.SlotSideOffset);
			}
			// v2.2: the triad's rear man straight behind at TriadRearBack (the diamond's at DiamondRearBack).
			return FVector2D(MemberCount == 3 ? -Config.TriadRearBack : -Config.DiamondRearBack, 0.f);
		case EFormationKind::Blizzard:
			if (bPair || Role == EFormationRole::Right)
			{
				return FVector2D(-Config.BlizzardBackOffset, Config.BlizzardSideOffset);
			}
			if (Role == EFormationRole::Left)
			{
				return FVector2D(-Config.BlizzardBackOffset, -Config.BlizzardSideOffset);
			}
			return FVector2D(-2.f * Config.BlizzardBackOffset, 0.f);
		case EFormationKind::Column:
			if (bPair || Role == EFormationRole::Right)
			{
				return FVector2D(-Config.ColumnBaseBack, Config.ColumnStagger);
			}
			if (Role == EFormationRole::Left)
			{
				return FVector2D(-Config.ColumnBaseBack - Config.ColumnSpacing, -Config.ColumnStagger);
			}
			// v2.2: with three the rear man closes up behind P.
			return FVector2D(-Config.ColumnBaseBack - (MemberCount == 3 ? 1.f : 2.f) * Config.ColumnSpacing, 0.f);
		case EFormationKind::Line:
			if (bPair || Role == EFormationRole::Right)
			{
				return FVector2D(0.f, Config.LineSpacing);
			}
			if (Role == EFormationRole::Left || MemberCount == 3)
			{
				// v2.2: with three Z stands on the left.
				return FVector2D(0.f, -Config.LineSpacing);
			}
			return FVector2D(0.f, 2.f * Config.LineSpacing);
		case EFormationKind::EchelonRight:
		case EFormationKind::EchelonLeft:
		{
			const float Index = static_cast<float>(EchelonIndex(Formation, Role, MemberCount));
			const float OpenSide = Formation == EFormationKind::EchelonRight ? 1.f : -1.f;
			return FVector2D(-Config.EchelonStep * Index, OpenSide * Config.EchelonStep * Index);
		}
		case EFormationKind::Hedgehog:
		{
			const float Radius = Config.HedgehogRadius;
			if (bPair)
			{
				return FVector2D(-2.f * Radius, 0.f);
			}
			if (MemberCount == 3)
			{
				// Triangle round the centre (-r, 0): K at 0 deg, P at +120, L at -120.
				const float Angle = FMath::DegreesToRadians(Role == EFormationRole::Right ? 120.f : -120.f);
				return FVector2D(-Radius + Radius * FMath::Cos(Angle), Radius * FMath::Sin(Angle));
			}
			if (Role == EFormationRole::Right)
			{
				return FVector2D(-Radius, Radius);
			}
			if (Role == EFormationRole::Left)
			{
				return FVector2D(-Radius, -Radius);
			}
			return FVector2D(-2.f * Radius, 0.f);
		}
		default:
			// Wall: not used (Slots returns no slots; FormationWallRules::Layout owns the wall).
			return FVector2D::ZeroVector;
		}
	}

	TArray<FFormationSlot> Slots(EFormationKind Formation, int32 MemberCount, const FVector2D& LeaderPos, float LeaderYaw, const FFormationConfig& Config)
	{
		TArray<FFormationSlot> Result;
		if (Formation == EFormationKind::Wall)
		{
			return Result;
		}
		for (const EFormationRole Role : Roles(MemberCount))
		{
			const FVector2D Local = LocalSlot(Formation, Role, MemberCount, Config);
			FFormationSlot Slot;
			Slot.Role = Role;
			Slot.Position = FormationMath::Offset(LeaderPos, LeaderYaw, static_cast<float>(Local.X), static_cast<float>(Local.Y));
			Slot.Yaw = FormationSectorRules::LookYaw(Role, Formation, EFormationMode::Halt, MemberCount, 0.f, LeaderYaw, TOptional<float>(), Config);
			Result.Add(Slot);
		}
		return Result;
	}

	TArray<FFormationSlot> TrailSlots(const TArray<FVector2D>& Trail, float LeaderYaw, int32 MemberCount, const FFormationConfig& Config,
		const TArray<FFormationWall>& Walls)
	{
		TArray<FFormationSlot> Result;
		if (Trail.Num() == 0)
		{
			return Result;
		}
		struct FTrailPiece
		{
			FVector2D From;
			FVector2D To;
			double Length;
			double Yaw;
		};
		const FVector2D LeaderPos = Trail.Last();
		// Pieces newest -> oldest, each with its travel direction.
		TArray<FTrailPiece> Pieces;
		for (int32 Index = Trail.Num() - 1; Index > 0; --Index)
		{
			const double Length = FVector2D::Distance(Trail[Index - 1], Trail[Index]);
			if (Length > 1e-6)
			{
				Pieces.Add({ Trail[Index], Trail[Index - 1], Length, FormationMath::YawOfD(Trail[Index] - Trail[Index - 1]) });
			}
		}
		const double TailYaw = Pieces.Num() > 0 ? Pieces.Last().Yaw : static_cast<double>(LeaderYaw);
		const FVector2D Tail = Pieces.Num() > 0 ? Pieces.Last().To : LeaderPos;
		const auto Back = [&Pieces, TailYaw, &Tail](double Distance, FVector2D& OutPos, double& OutYaw)
		{
			double Left = Distance;
			for (const FTrailPiece& Piece : Pieces)
			{
				if (Left <= Piece.Length)
				{
					OutPos = Piece.From + (Piece.To - Piece.From) * (Left / Piece.Length);
					OutYaw = Piece.Yaw;
					return;
				}
				Left -= Piece.Length;
			}
			OutPos = Tail + FormationMath::DirOf(TailYaw) * -Left;
			OutYaw = TailYaw;
		};
		for (const EFormationRole Role : Roles(MemberCount))
		{
			FFormationSlot Slot;
			Slot.Role = Role;
			if (Role == EFormationRole::Commander)
			{
				Slot.Position = LeaderPos;
				Slot.Yaw = LeaderYaw;
			}
			else
			{
				const FVector2D Local = LocalSlot(EFormationKind::Column, Role, MemberCount, Config);
				FVector2D At;
				double AtYaw = 0.0;
				Back(-Local.X, At, AtYaw);
				// v2.3: the stagger shrinks (5 cm steps, down to 0) until the slot keeps ColumnStaggerClear off every wall surface and
				// the line from its trail point to it is clear.
				const FVector2D Side = FormationMath::DirOf(AtYaw + 90.0);
				const double Clear = Config.ColumnStaggerClear;
				const double Sign = FMath::Sign(Local.Y);
				const auto Fits = [&Walls, &At, Clear](const FVector2D& P)
				{
					for (const FFormationWall& Wall : Walls)
					{
						if (FormationMath::DistToWall(P, Wall) < Clear - 1e-6)
						{
							return false;
						}
					}
					return !FormationMath::SegmentBlocked(At, P, Walls, 0.0);
				};
				double Stagger = FMath::Abs(Local.Y);
				while (Stagger > 0.0 && !Fits(At + Side * (Sign * Stagger)))
				{
					Stagger = FMath::Max(0.0, Stagger - 5.0);
				}
				Slot.Position = At + Side * (Sign * Stagger);
				Slot.Yaw = static_cast<float>(AtYaw);
			}
			Result.Add(Slot);
		}
		return Result;
	}

	TArray<FFormationSlot> HedgehogSlots(const FVector2D& Center, int32 MemberCount, float Yaw, const FFormationConfig& Config)
	{
		const FVector2D LeaderPos = Center + FormationMath::Forward(Yaw) * static_cast<double>(Config.HedgehogRadius);
		// Slots fills Yaw with the halt look (outward on the ring).
		return Slots(EFormationKind::Hedgehog, MemberCount, LeaderPos, Yaw, Config);
	}
}
