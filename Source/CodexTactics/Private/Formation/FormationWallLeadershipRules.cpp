#include "Formation/FormationWallLeadershipRules.h"

#include "Formation/FormationSectorRules.h"
#include "Formation/FormationWallRules.h"

namespace FormationWallLeadership
{
	FVector2D TowardCorner(float NormalYaw, const FVector2D& EdgeOnFace, const FVector2D& Reference)
	{
		const FVector2D RightAlong = FormationMath::Forward(NormalYaw + 270.f);
		return FVector2D::DotProduct(EdgeOnFace - Reference, RightAlong) >= 0.0 ? RightAlong : -RightAlong;
	}

	bool IsAtWallForMoveLead(bool bWallOrder, bool bWallLayoutActive, bool bCornerPeek, bool bTwoCornerHold)
	{
		return bWallOrder || bWallLayoutActive || bCornerPeek || bTwoCornerHold;
	}

	int32 PickMoveLeader(const TArray<double>& RouteCm, const TArray<bool>& Able, int32 CurrentLeader, double KeepCm)
	{
		int32 Best = INDEX_NONE;
		for (int32 Index = 0; Index < RouteCm.Num() && Index < Able.Num(); ++Index)
		{
			if (Able[Index] && RouteCm[Index] >= 0.0 && (Best == INDEX_NONE || RouteCm[Index] < RouteCm[Best]))
			{
				Best = Index;
			}
		}
		const bool bCurrentFits = RouteCm.IsValidIndex(CurrentLeader) && Able.IsValidIndex(CurrentLeader) && Able[CurrentLeader] && RouteCm[CurrentLeader] >= 0.0;
		if (Best != INDEX_NONE && bCurrentFits && RouteCm[CurrentLeader] <= RouteCm[Best] + KeepCm)
		{
			return CurrentLeader;
		}
		return Best;
	}

	int32 PickCornerLeader(const TArray<FWallLeadershipMember>& Members, const FVector2D& EdgeOnFace, const FVector2D& Toward)
	{
		int32 Best = INDEX_NONE;
		for (int32 Index = 0; Index < Members.Num(); ++Index)
		{
			const FWallLeadershipMember& Each = Members[Index];
			if (!Each.bAble)
			{
				continue;
			}
			if (Best == INDEX_NONE)
			{
				Best = Index;
				continue;
			}
			const double Ahead = FVector2D::DotProduct(Each.Position - Members[Best].Position, Toward);
			if (Ahead > TieCm
				|| (Ahead >= -TieCm && FVector2D::Distance(Each.Position, EdgeOnFace) < FVector2D::Distance(Members[Best].Position, EdgeOnFace)))
			{
				Best = Index;
			}
		}
		return Best;
	}

	TArray<int32> StackOrder(const TArray<FWallLeadershipMember>& Members, int32 LeaderIndex, const FVector2D& Toward)
	{
		TArray<int32> Able;
		TArray<int32> Unable;
		for (int32 Index = 0; Index < Members.Num(); ++Index)
		{
			if (Index != LeaderIndex)
			{
				(Members[Index].bAble ? Able : Unable).Add(Index);
			}
		}
		const auto NearestFirst = [&Members, &Toward](int32 A, int32 B)
		{
			const double AlongA = FVector2D::DotProduct(Members[A].Position, Toward);
			const double AlongB = FVector2D::DotProduct(Members[B].Position, Toward);
			return AlongA != AlongB ? AlongA > AlongB : A < B;
		};
		Able.Sort(NearestFirst);
		Unable.Sort(NearestFirst);
		Able.Append(Unable);
		return Able;
	}

	TArray<EFormationRole> SwapRoles(const TArray<FWallLeadershipMember>& Members, int32 NewLeader)
	{
		TArray<EFormationRole> Roles;
		for (const FWallLeadershipMember& Each : Members)
		{
			Roles.Add(Each.Role);
		}
		if (!Members.IsValidIndex(NewLeader) || Members[NewLeader].Role == EFormationRole::Commander)
		{
			return Roles;
		}
		const EFormationRole Freed = Members[NewLeader].Role;
		for (int32 Index = 0; Index < Roles.Num(); ++Index)
		{
			if (Index != NewLeader && Roles[Index] == EFormationRole::Commander)
			{
				Roles[Index] = Freed;
			}
		}
		Roles[NewLeader] = EFormationRole::Commander;
		return Roles;
	}

	TArray<FFormationSlot> StackLine(const FVector2D& KPlace, const FVector2D& Toward, const TArray<EFormationRole>& RolesBehind, float Spacing, float FirstGapCm)
	{
		TArray<FFormationSlot> Line;
		FFormationSlot Lead;
		Lead.Role = EFormationRole::Commander;
		Lead.Position = KPlace;
		Line.Add(Lead);
		for (int32 Rank = 0; Rank < RolesBehind.Num(); ++Rank)
		{
			FFormationSlot Place;
			Place.Role = RolesBehind[Rank];
			Place.Position = KPlace - Toward * (static_cast<double>(Rank + 1) * Spacing + FirstGapCm);
			Line.Add(Place);
		}
		return Line;
	}

	FFormationStackEnd EndAtEdge(const FVector2D& EdgeOnFace, const FVector2D& Toward)
	{
		FFormationStackEnd End;
		End.End = EdgeOnFace;
		End.Along = Toward;
		End.Thickness = 0.f;
		End.Wall = INDEX_NONE;
		return End;
	}

	FWallLeadershipPlan Plan(const TArray<FFormationWall>& Walls, const FVector2D& LineOrigin, float NormalYaw, const FVector2D& EdgeOnFace,
		const TArray<FWallLeadershipMember>& Members, const FFormationConfig& Config)
	{
		FWallLeadershipPlan Result;
		Result.Toward = TowardCorner(NormalYaw, EdgeOnFace, LineOrigin);
		Result.Leader = PickCornerLeader(Members, EdgeOnFace, Result.Toward);
		if (Result.Leader == INDEX_NONE)
		{
			return Result;
		}
		Result.Order = StackOrder(Members, Result.Leader, Result.Toward);
		Result.Roles = SwapRoles(Members, Result.Leader);
		// His place on the layout's line (the face offset kept): where he stands, along the wall.
		const FVector2D RightAlong = FormationMath::Forward(NormalYaw + 270.f);
		const FVector2D KPlace = LineOrigin + RightAlong * FVector2D::DotProduct(Members[Result.Leader].Position - LineOrigin, RightAlong);
		TArray<EFormationRole> Behind;
		for (const int32 Index : Result.Order)
		{
			Behind.Add(Result.Roles[Index]);
		}
		const TArray<FFormationSlot> Line = StackLine(KPlace, Result.Toward, Behind, Config.WallSpacing, static_cast<float>(FormationWallEnd::AtEndGapCm));
		// The corner: the free end of the squad's wall (a move out from the edge); already at it -> pushed on so the move still reaches it.
		FVector2D Destination = EdgeOnFace + FormationMath::Forward(NormalYaw) * FormationWallEnd::PeekDestinationOutCm;
		if (FMath::Abs(FVector2D::DotProduct(Destination - KPlace, Result.Toward)) < FormationWallRules::SlideMin)
		{
			Destination += Result.Toward * FormationWallRules::SlideMin;
		}
		const TOptional<FFormationStackEnd> WorldEnd = FormationWallEnd::FindStackEnd(Walls, KPlace, NormalYaw, Destination, Config);
		Result.bWorldEnd = WorldEnd.IsSet() && FVector2D::DotProduct(WorldEnd->Along, Result.Toward) > 0.5;
		Result.End = Result.bWorldEnd ? WorldEnd.GetValue() : EndAtEdge(EdgeOnFace, Result.Toward);
		Result.StackSlots = FormationWallEnd::StackSlots(Line, Result.End, NormalYaw, Config);
		return Result;
	}

	FVector2D HoldBehind(const FVector2D& Target, const FVector2D& MyPlace, const FVector2D& AheadPlace, const FVector2D& AheadPos, const FVector2D& Toward,
		double SlackCm)
	{
		const double Lag = FMath::Max(0.0, FVector2D::DotProduct(AheadPlace - MyPlace, Toward));
		const double MaxAlong = FVector2D::DotProduct(AheadPos, Toward) - Lag + SlackCm;
		const double Over = FVector2D::DotProduct(Target, Toward) - MaxAlong;
		return Over > 0.0 ? Target - Toward * Over : Target;
	}

	bool ReformStands(double DistanceToPlaceCm, bool bCrouched, double StandCm, bool bAlongWall)
	{
		return !bAlongWall && bCrouched && StandCm > 0.0 && DistanceToPlaceCm > StandCm;
	}

	bool ReformRearFacesWalk(double DistanceToPlaceCm, bool bTurned, const FFormationConfig& Config, bool bAlongWall)
	{
		return !bAlongWall && FormationSectorRules::RearCatchUp(static_cast<float>(DistanceToPlaceCm), bTurned, /*bWayBlocked*/ false, Config);
	}

	bool ReformAlongWall(const FVector2D& MemberPos, const FVector2D& PlacePos, float NormalYaw, double OffCm)
	{
		return OffCm > 0.0 && FMath::Abs(FVector2D::DotProduct(MemberPos - PlacePos, FormationMath::Forward(NormalYaw))) <= OffCm;
	}

	FGroupCornerPeekPlan PlanGroupCornerPeek(const TArray<double>& MemberAlong, const TArray<bool>& Able, double EdgeAlong, double PeekerStandAlong,
		double SpacingCm, double KeepCm)
	{
		FGroupCornerPeekPlan Plan;
		if (MemberAlong.IsEmpty() || Able.Num() != MemberAlong.Num())
		{
			return Plan;
		}
		// The corner's side: from the group's middle towards the clicked end.
		double Sum = 0.0;
		for (const double Along : MemberAlong)
		{
			Sum += Along;
		}
		Plan.Toward = EdgeAlong >= Sum / MemberAlong.Num() ? 1.0 : -1.0;
		for (int32 Index = 0; Index < MemberAlong.Num(); ++Index)
		{
			if (!Able[Index])
			{
				continue;
			}
			if (Plan.Peeker == INDEX_NONE)
			{
				Plan.Peeker = Index;
				continue;
			}
			const double Ahead = (MemberAlong[Index] - MemberAlong[Plan.Peeker]) * Plan.Toward;
			if (Ahead > TieCm || (Ahead >= -TieCm && FMath::Abs(EdgeAlong - MemberAlong[Index]) < FMath::Abs(EdgeAlong - MemberAlong[Plan.Peeker])))
			{
				Plan.Peeker = Index;
			}
		}
		if (Plan.Peeker == INDEX_NONE)
		{
			return Plan;
		}
		for (int32 Index = 0; Index < MemberAlong.Num(); ++Index)
		{
			if (Index != Plan.Peeker && Able[Index])
			{
				Plan.Order.Add(Index);
			}
		}
		const double Toward = Plan.Toward;
		Plan.Order.StableSort([&MemberAlong, Toward](const int32 A, const int32 B) { return MemberAlong[A] * Toward > MemberAlong[B] * Toward; });
		for (int32 Position = 0; Position < Plan.Order.Num(); ++Position)
		{
			const double Place = PeekerStandAlong - Toward * SpacingCm * (Position + 1);
			const double Here = MemberAlong[Plan.Order[Position]];
			Plan.PlaceAlong.Add(FMath::Abs(Here - Place) <= KeepCm ? Here : Place);
		}
		return Plan;
	}

	int32 AheadInLine(const TArray<bool>& Able, int32 Position)
	{
		for (int32 Before = FMath::Min(Position, Able.Num()) - 1; Before >= 0; --Before)
		{
			if (Able[Before])
			{
				return Before;
			}
		}
		return INDEX_NONE;
	}
}
