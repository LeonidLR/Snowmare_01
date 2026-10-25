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
			return { EFormationRole::Commander, EFormationRole::Right, EFormationRole::Left };
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
			return FVector2D(-Config.DiamondRearBack, 0.f);
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
			return FVector2D(-Config.ColumnBaseBack - 2.f * Config.ColumnSpacing, 0.f);
		case EFormationKind::Line:
			if (bPair || Role == EFormationRole::Right)
			{
				return FVector2D(0.f, Config.LineSpacing);
			}
			if (Role == EFormationRole::Left)
			{
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

	TArray<FFormationSlot> HedgehogSlots(const FVector2D& Center, int32 MemberCount, float Yaw, const FFormationConfig& Config)
	{
		const FVector2D LeaderPos = Center + FormationMath::Forward(Yaw) * static_cast<double>(Config.HedgehogRadius);
		// Slots fills Yaw with the halt look (outward on the ring).
		return Slots(EFormationKind::Hedgehog, MemberCount, LeaderPos, Yaw, Config);
	}
}
