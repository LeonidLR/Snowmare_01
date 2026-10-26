#include "Formation/FormationWallRules.h"

#include "Formation/FormationCoverRules.h"
#include "Formation/FormationSectorRules.h"

namespace FormationWallRules
{
	/** The point of A -> B nearest to P (the parameter clamped to the segment). */
	static FVector2D WallClosestPoint(const FVector2D& P, const FVector2D& A, const FVector2D& B)
	{
		const FVector2D Edge = B - A;
		const double LengthSquared = FVector2D::DotProduct(Edge, Edge);
		const double T = LengthSquared == 0.0 ? 0.0 : FMath::Clamp(FVector2D::DotProduct(P - A, Edge) / LengthSquared, 0.0, 1.0);
		return A + Edge * T;
	}

	static FVector2D WallNormalAt(const FFormationWall& Wall, const FVector2D& ClickedSlot, const FVector2D& Closest)
	{
		FVector2D Normal = ClickedSlot - Closest;
		if (Normal.Size() < 1e-6)
		{
			const FVector2D Edge = Wall.B - Wall.A;
			Normal = FVector2D(-Edge.Y, Edge.X);
		}
		return Normal;
	}

	float OutwardYaw(const FFormationWall& Wall, const FVector2D& ClickedSlot)
	{
		return FormationMath::YawOf(WallNormalAt(Wall, ClickedSlot, WallClosestPoint(ClickedSlot, Wall.A, Wall.B)));
	}

	TArray<FFormationSlot> Layout(const FFormationWall& Wall, const FVector2D& ClickedSlot, int32 MemberCount, float RoomLeft, float RoomRight,
		const FFormationConfig& Config)
	{
		const FVector2D Closest = WallClosestPoint(ClickedSlot, Wall.A, Wall.B);
		const float NormalYaw = FormationMath::YawOf(WallNormalAt(Wall, ClickedSlot, Closest));
		const FVector2D Anchor = Closest + FormationMath::Forward(NormalYaw) * (0.5 * Wall.Thickness + Config.WallFaceOffset);
		const FVector2D RightAlong = FormationMath::Forward(NormalYaw + 270.f);
		const auto At = [&Anchor, &RightAlong, &Config](float Steps) { return Anchor + RightAlong * (Steps * Config.WallSpacing); };
		const auto Look = [NormalYaw](float Relative) { return FRotator::NormalizeAxis(NormalYaw + Relative); };
		const auto Make = [](EFormationRole Role, const FVector2D& Position, float Yaw)
		{
			FFormationSlot Slot;
			Slot.Role = Role;
			Slot.Position = Position;
			Slot.Yaw = Yaw;
			return Slot;
		};
		const bool bRightEnd = RoomRight >= RoomLeft;
		const float Flank = Config.WallFlankDeg;
		const float Neighbour = Config.WallNeighbourDeg;
		const float End = Config.WallEndDeg;
		using ER = EFormationRole;

		if (MemberCount <= 1)
		{
			return { Make(ER::Commander, Anchor, Look(0.f)) };
		}
		if (MemberCount == 2)
		{
			return bRightEnd ? TArray<FFormationSlot>{ Make(ER::Commander, Anchor, Look(PairLookDeg)), Make(ER::Rear, At(1.f), Look(-PairLookDeg)) }
				: TArray<FFormationSlot>{ Make(ER::Commander, Anchor, Look(-PairLookDeg)), Make(ER::Rear, At(-1.f), Look(PairLookDeg)) };
		}
		if (MemberCount == 3)
		{
			// v2.2: the triad is K, P, Z - the man on the LEFT end (Z) looks 8: the user's wall rule is by position.
			return { Make(ER::Commander, Anchor, Look(0.f)), Make(ER::Right, At(1.f), Look(-Flank)), Make(ER::Rear, At(-1.f), Look(Flank)) };
		}
		if (bRightEnd)
		{
			return { Make(ER::Commander, Anchor, Look(0.f)), Make(ER::Right, At(1.f), Look(-Neighbour)), Make(ER::Left, At(-1.f), Look(Flank)),
				Make(ER::Rear, At(2.f), Look(-End)) };
		}
		return { Make(ER::Commander, Anchor, Look(0.f)), Make(ER::Right, At(1.f), Look(-Flank)), Make(ER::Left, At(-1.f), Look(Neighbour)),
			Make(ER::Rear, At(-2.f), Look(End)) };
	}

	void WallRoom(const FFormationWall& Wall, const FVector2D& ClickedSlot, float& OutRoomLeft, float& OutRoomRight)
	{
		const FVector2D Closest = WallClosestPoint(ClickedSlot, Wall.A, Wall.B);
		// As the reference: the normal from the nearest point (a slot ON the line gives yaw 0).
		const FVector2D RightAlong = FormationMath::Forward(FormationMath::YawOf(ClickedSlot - Closest) + 270.f);
		const double SideA = FVector2D::DotProduct(Wall.A - Closest, RightAlong);
		const double SideB = FVector2D::DotProduct(Wall.B - Closest, RightAlong);
		OutRoomRight = static_cast<float>(FMath::Max3(SideA, SideB, 0.0));
		OutRoomLeft = static_cast<float>(FMath::Max3(-SideA, -SideB, 0.0));
	}

	FFormationSlideResult Slide(const FFormationWall& Wall, const TArray<FFormationSlot>& StartSlots, const FVector2D& LeaderPos, const FVector2D& Destination,
		float Time, const FFormationConfig& Config)
	{
		using FormationMath::NormYaw;
		FFormationSlideResult Result;
		if (StartSlots.Num() == 0)
		{
			return Result;
		}
		const FVector2D K0 = StartSlots[0].Position;
		const double Length = FVector2D::Distance(Wall.A, Wall.B);
		const FVector2D U = Length > 1e-6 ? (Wall.B - Wall.A) * (1.0 / Length) : FVector2D(1.0, 0.0);
		const FVector2D Foot = FormationMath::ClosestOnSegment(K0, Wall.A, Wall.B);
		FVector2D Normal = K0 - Foot;
		if (Normal.Size() < 1e-6)
		{
			Normal = FVector2D(-U.Y, U.X);
		}
		const FVector2D N = Normal * (1.0 / Normal.Size());
		const double NormalYaw = FormationMath::YawOfD(N);
		double DestT = 0.0;
		const FVector2D DestFoot = FormationMath::ClosestOnSegment(Destination, Wall.A, Wall.B, &DestT);
		const double Off = FVector2D::Distance(Destination, DestFoot) - 0.5 * Wall.Thickness;
		const double AlongDest = FVector2D::DotProduct(Destination - K0, U);
		const bool bSameSide = FVector2D::DotProduct(Destination - DestFoot, N) >= 0.0;
		const bool bInside = DestT > 0.0 && DestT < 1.0;
		Result.bActive = Off <= static_cast<double>(Config.SlideMaxOffWall) && bSameSide && bInside && FMath::Abs(AlongDest) >= SlideMin;
		Result.NormalYaw = static_cast<float>(NormalYaw);
		if (!Result.bActive)
		{
			Result.LeaderTarget = K0;
			Result.Targets = StartSlots;
			for (const FFormationSlot& Slot : StartSlots)
			{
				Result.Order.Add(Slot.Role);
			}
			return Result;
		}
		Result.LeaderTarget = K0 + U * AlongDest;
		const FVector2D D = U * FMath::Sign(AlongDest);
		const double Moved = FVector2D::DotProduct(LeaderPos - K0, D);
		const FVector2D Shift = D * FMath::Max(0.0, FMath::Min(FMath::Abs(AlongDest), Moved));
		TArray<FFormationSlot> Sorted = StartSlots;
		Sorted.StableSort([&D](const FFormationSlot& A, const FFormationSlot& B) { return FVector2D::DotProduct(A.Position, D) > FVector2D::DotProduct(B.Position, D); });
		for (const FFormationSlot& Slot : Sorted)
		{
			Result.Order.Add(Slot.Role);
		}
		const EFormationRole LeadRole = Result.Order[0];
		const EFormationRole TrailRole = Result.Order.Last();
		const double Out = Config.SlideLeadOutDeg;
		const double DYaw = FormationMath::YawOfD(D);
		// "Outward" = turned from the wall direction towards the normal.
		const auto TowardNormal = [NormalYaw, Out](double Yaw)
		{
			const double Sign = FMath::Sign(NormYaw(NormalYaw - Yaw));
			return NormYaw(Yaw + (Sign != 0.0 ? Sign : 1.0) * Out);
		};
		const auto PosOfRole = [&StartSlots, &Shift](EFormationRole Role)
		{
			const FFormationSlot* Slot = StartSlots.FindByPredicate([Role](const FFormationSlot& Candidate) { return Candidate.Role == Role; });
			return (Slot ? Slot->Position : FVector2D::ZeroVector) + Shift;
		};
		// Looks in insertion order (a later set of the same role keeps its place, as a JS Map).
		TArray<FFormationRoleYaw> Looks;
		const auto SetLook = [&Looks](EFormationRole Role, double Yaw)
		{
			if (FFormationRoleYaw* Existing = Looks.FindByPredicate([Role](const FFormationRoleYaw& Look) { return Look.Role == Role; }))
			{
				Existing->Yaw = static_cast<float>(Yaw);
			}
			else
			{
				Looks.Add({ Role, static_cast<float>(Yaw) });
			}
		};
		const auto HasLook = [&Looks](EFormationRole Role) { return Looks.ContainsByPredicate([Role](const FFormationRoleYaw& Look) { return Look.Role == Role; }); };
		SetLook(LeadRole, TowardNormal(DYaw));
		SetLook(TrailRole, TowardNormal(NormYaw(DYaw + 180.0)));
		const EFormationRole KRole = StartSlots[0].Role;
		if (!HasLook(KRole))
		{
			SetLook(KRole, NormYaw(NormalYaw + FormationSectorRules::ScanValue(0.f, Config.ScanAmpLead, Config.ScanPeriod, FormationSectorRules::LeadScanPhase, Time,
				Config.ScanDwellExp)));
		}
		TArray<EFormationRole> Middle;
		for (const EFormationRole Role : Result.Order)
		{
			if (!HasLook(Role))
			{
				Middle.Add(Role);
			}
		}
		if (Middle.Num() > 0)
		{
			TArray<FFormationSlot> Members;
			for (const FFormationSlot& Slot : StartSlots)
			{
				FFormationSlot Member;
				Member.Role = Slot.Role;
				Member.Position = PosOfRole(Slot.Role);
				Members.Add(Member);
			}
			const TArray<FFormationRoleYaw> Fixed = Looks;
			for (const FFormationRoleYaw& Look : FormationCoverRules::AllocateStatic({ Wall }, Members, Fixed, Middle, Config))
			{
				SetLook(Look.Role, Look.Yaw);
			}
		}
		Result.Lead = LeadRole;
		if (StartSlots.Num() >= 2)
		{
			Result.RearGuard = TrailRole;
		}
		for (const FFormationSlot& Slot : StartSlots)
		{
			FFormationSlot Target;
			Target.Role = Slot.Role;
			Target.Position = PosOfRole(Slot.Role);
			const FFormationRoleYaw* Look = Looks.FindByPredicate([&Slot](const FFormationRoleYaw& Candidate) { return Candidate.Role == Slot.Role; });
			Target.Yaw = Look ? Look->Yaw : Slot.Yaw;
			Result.Targets.Add(Target);
		}
		return Result;
	}

	/** The probe ring (rules.ts probeRing): per member and point, a clear line (no wall body within WallEndRayRadius) and the direction. */
	static void WallEndProbeRing(const TArray<FFormationWall>& Walls, const TArray<FVector2D>& Positions, double Probe, TArray<TArray<bool>>& OutVisible,
		TArray<TArray<double>>& OutDirections)
	{
		FVector2D Centre = FVector2D::ZeroVector;
		for (const FVector2D& Position : Positions)
		{
			Centre += Position;
		}
		if (Positions.Num() > 0)
		{
			Centre *= 1.0 / static_cast<double>(Positions.Num());
		}
		const int32 Count = FMath::RoundToInt32(360.0 / WallEndStepDeg);
		TArray<FVector2D> Points;
		Points.Reserve(Count);
		for (int32 Index = 0; Index < Count; ++Index)
		{
			Points.Add(Centre + FormationMath::DirOf(-180.0 + Index * WallEndStepDeg) * Probe);
		}
		OutVisible.SetNum(Positions.Num());
		OutDirections.SetNum(Positions.Num());
		for (int32 Member = 0; Member < Positions.Num(); ++Member)
		{
			OutVisible[Member].SetNum(Count);
			OutDirections[Member].SetNum(Count);
			for (int32 Index = 0; Index < Count; ++Index)
			{
				OutVisible[Member][Index] = !FormationMath::SegmentBlocked(Positions[Member], Points[Index], Walls, WallEndRayRadius);
				OutDirections[Member][Index] = FormationMath::YawOfD(Points[Index] - Positions[Member]);
			}
		}
	}

	/** The share of the open ring points (some member sees them) inside the cone of a member who sees them. */
	static FFormationArcCoverage WallEndCoverage(const TArray<TArray<bool>>& Visible, const TArray<TArray<double>>& Directions, const TArray<double>& Looks,
		double Half)
	{
		FFormationArcCoverage Result;
		const int32 Count = Visible.Num() > 0 ? Visible[0].Num() : 0;
		for (int32 Index = 0; Index < Count; ++Index)
		{
			bool bOpen = false;
			bool bCovered = false;
			for (int32 Member = 0; Member < Visible.Num(); ++Member)
			{
				if (!Visible[Member][Index])
				{
					continue;
				}
				bOpen = true;
				bCovered |= FMath::Abs(FormationMath::NormYaw(Directions[Member][Index] - Looks[Member])) <= Half + 1e-9;
			}
			Result.Open += bOpen ? 1 : 0;
			Result.Covered += bCovered ? 1 : 0;
		}
		Result.Pct = Result.Open > 0 ? 100.0 * static_cast<double>(Result.Covered) / static_cast<double>(Result.Open) : 100.0;
		return Result;
	}

	FFormationArcCoverage OpenArcCoverage(const TArray<FFormationWall>& Walls, const TArray<FVector2D>& Positions, const TArray<float>& LookYaws,
		float FovDeg, float ProbeCm)
	{
		TArray<TArray<bool>> Visible;
		TArray<TArray<double>> Directions;
		WallEndProbeRing(Walls, Positions, static_cast<double>(ProbeCm), Visible, Directions);
		TArray<double> Looks;
		for (int32 Member = 0; Member < Positions.Num(); ++Member)
		{
			Looks.Add(LookYaws.IsValidIndex(Member) ? static_cast<double>(LookYaws[Member]) : 0.0);
		}
		return WallEndCoverage(Visible, Directions, Looks, 0.5 * static_cast<double>(FovDeg));
	}

	FFormationWallEndResult EndLooks(const TArray<FFormationWall>& Walls, const TArray<FFormationSlot>& Slots, const FFormationConfig& Config)
	{
		using FormationMath::NormYaw;
		FFormationWallEndResult Result;
		Result.Slots = Slots;
		TArray<FVector2D> Positions;
		TArray<double> Looks;
		for (const FFormationSlot& Slot : Slots)
		{
			Positions.Add(Slot.Position);
			Looks.Add(static_cast<double>(Slot.Yaw));
		}
		const double Half = 0.5 * static_cast<double>(Config.SightFov);
		TArray<TArray<bool>> Visible;
		TArray<TArray<double>> Directions;
		WallEndProbeRing(Walls, Positions, static_cast<double>(Config.WallEndProbeDist), Visible, Directions);
		Result.BasePct = WallEndCoverage(Visible, Directions, Looks, Half).Pct;
		Result.Pct = Result.BasePct;
		if (Slots.Num() < 3 || Result.BasePct >= static_cast<double>(Config.WallEndCoverPct) - 1e-9)
		{
			return Result; // a face layout (or the pair): the user's looks
		}
		const int32 Count = Visible[0].Num();
		const double Reach = Config.LookWallDist;
		const double WallAngle = Config.LookWallAngle;
		// Directions into a wall next to each slot (ClearLook's blocked spans from his own slot).
		TArray<TArray<double>> Centers;
		Centers.SetNum(Slots.Num());
		for (int32 Member = 0; Member < Slots.Num(); ++Member)
		{
			for (const FFormationWall& Wall : Walls)
			{
				const FVector2D Closest = FormationMath::ClosestOnSegment(Positions[Member], Wall.A, Wall.B);
				const double Distance = FVector2D::Distance(Positions[Member], Closest);
				if (Distance < 1e-6 || FMath::Max(0.0, Distance - 0.5 * Wall.Thickness) > Reach)
				{
					continue;
				}
				Centers[Member].Add(FormationMath::YawOfD(Closest - Positions[Member]));
			}
		}
		TArray<bool> Held;
		Held.Init(false, Count);
		const auto Hold = [&](int32 Member, double Yaw)
		{
			for (int32 Index = 0; Index < Count; ++Index)
			{
				if (Visible[Member][Index] && FMath::Abs(NormYaw(Directions[Member][Index] - Yaw)) <= Half + 1e-9)
				{
					Held[Index] = true;
				}
			}
		};
		TArray<double> Spread = Looks;
		Hold(0, Spread[0]); // K keeps his look (away from the wall)
		TArray<int32> Left;
		for (int32 Member = 1; Member < Slots.Num(); ++Member)
		{
			Left.Add(Member);
		}
		while (Left.Num() > 0)
		{
			int32 BestMember = INDEX_NONE;
			int32 BestGain = -1;
			double BestYaw = 0.0;
			for (const int32 Member : Left)
			{
				int32 MineGain = -1;
				double MineYaw = 0.0;
				double MineOff = 0.0;
				for (int32 Degree = -179; Degree <= 180; ++Degree)
				{
					const double Yaw = static_cast<double>(Degree);
					bool bIntoWall = false;
					for (const double Center : Centers[Member])
					{
						bIntoWall |= FMath::Abs(NormYaw(Yaw - Center)) < WallAngle - 1e-6;
					}
					if (bIntoWall)
					{
						continue; // nobody looks into a wall
					}
					int32 Gain = 0;
					for (int32 Index = 0; Index < Count; ++Index)
					{
						if (!Held[Index] && Visible[Member][Index] && FMath::Abs(NormYaw(Directions[Member][Index] - Yaw)) <= Half + 1e-9)
						{
							++Gain;
						}
					}
					const double Off = FMath::Abs(NormYaw(Yaw - Looks[Member]));
					if (MineGain < 0 || Gain > MineGain || (Gain == MineGain && Off < MineOff - 1e-9))
					{
						MineGain = Gain;
						MineYaw = Yaw;
						MineOff = Off;
					}
				}
				if (MineGain >= 0 && (BestMember == INDEX_NONE || MineGain > BestGain))
				{
					BestMember = Member;
					BestGain = MineGain;
					BestYaw = MineYaw;
				}
			}
			if (BestMember == INDEX_NONE || BestGain == 0)
			{
				break; // nothing left anybody could add: the rest keep their layout looks
			}
			Spread[BestMember] = BestYaw;
			Hold(BestMember, BestYaw);
			Left.Remove(BestMember);
		}
		const double SpreadPct = WallEndCoverage(Visible, Directions, Spread, Half).Pct;
		if (SpreadPct > Result.BasePct + 1e-9)
		{
			Result.bSpread = true;
			Result.Pct = SpreadPct;
			for (int32 Member = 0; Member < Slots.Num(); ++Member)
			{
				Result.Slots[Member].Yaw = static_cast<float>(NormYaw(Spread[Member]));
			}
		}
		return Result;
	}

	FFormationApproachResult Approach(const TArray<FFormationWall>& Walls, const TArray<FFormationSlot>& Slots, float NormalYaw,
		const TArray<FFormationSlot>& Members, const TOptional<FFormationApproachState>& InState, const FFormationConfig& Config)
	{
		using FormationMath::NormYaw;
		FFormationApproachResult Result;
		if (Slots.Num() == 0)
		{
			return Result;
		}
		const double Normal = static_cast<double>(NormalYaw);
		const FVector2D N = FormationMath::DirOf(Normal);
		const FVector2D U = FormationMath::DirOf(Normal + 90.0);
		const auto PosOf = [&](EFormationRole Role)
		{
			if (const FFormationSlot* Member = Members.FindByPredicate([Role](const FFormationSlot& Each) { return Each.Role == Role; }))
			{
				return Member->Position;
			}
			const FFormationSlot* Slot = Slots.FindByPredicate([Role](const FFormationSlot& Each) { return Each.Role == Role; });
			return Slot ? Slot->Position : FVector2D::ZeroVector;
		};
		FFormationApproachState State;
		if (InState.IsSet())
		{
			State = InState.GetValue();
		}
		else
		{
			State.Runner = Slots[0].Role;
			const FVector2D Leader = PosOf(State.Runner);
			// The rear cover: the follower already looking nearest the rear (the patrol's rear guard - no gap while he turns); ties: the
			// farthest from the wall along the normal, then the later one.
			double RearOff = TNumericLimits<double>::Max();
			double RearD = -TNumericLimits<double>::Max();
			for (int32 Index = 1; Index < Slots.Num(); ++Index)
			{
				const EFormationRole Role = Slots[Index].Role;
				const FFormationSlot* Member = Members.FindByPredicate([Role](const FFormationSlot& Each) { return Each.Role == Role; });
				const double Off = Member ? FMath::Abs(NormYaw(static_cast<double>(Member->Yaw) - Normal)) : 180.0;
				const double D = FVector2D::DotProduct(PosOf(Role) - Slots[0].Position, N);
				if (!State.Rear.IsSet() || Off < RearOff - 1e-6 || (FMath::Abs(Off - RearOff) <= 1e-6 && D >= RearD - 1e-9))
				{
					State.Rear = Role;
					RearOff = Off;
					RearD = D;
				}
			}
			const auto Lateral = [&](EFormationRole Role) { return FVector2D::DotProduct(PosOf(Role) - Leader, U); };
			TArray<TPair<EFormationRole, double>> Others;
			for (int32 Index = 1; Index < Slots.Num(); ++Index)
			{
				if (!State.Rear.IsSet() || Slots[Index].Role != State.Rear.GetValue())
				{
					Others.Add({ Slots[Index].Role, Lateral(Slots[Index].Role) });
				}
			}
			// The lowest offset (a stable sort: ties keep the slot order) is the -1 side.
			int32 Lowest = 0;
			for (int32 Index = 1; Index < Others.Num(); ++Index)
			{
				if (Others[Index].Value < Others[Lowest].Value)
				{
					Lowest = Index;
				}
			}
			for (int32 Index = 0; Index < Others.Num(); ++Index)
			{
				const int32 Side = Others.Num() == 1 ? (Others[Index].Value >= 0.0 ? 1 : -1) : (Index == Lowest ? -1 : 1);
				State.Flanks.Add({ Others[Index].Key, Side });
			}
			State.AlongSide = State.Flanks.Num() > 0 ? -State.Flanks[0].Value : (State.Rear.IsSet() && Lateral(State.Rear.GetValue()) >= 0.0 ? -1 : 1);
		}
		const double Arrive = Config.ApproachArriveDist;
		const auto Arrived = [&](const FFormationSlot& Slot) { return FVector2D::Distance(PosOf(Slot.Role), Slot.Position) <= Arrive + 1e-9; };
		if (!State.bDone)
		{
			bool bAll = true;
			for (const FFormationSlot& Slot : Slots)
			{
				bAll &= Arrived(Slot);
			}
			State.bDone = bAll;
		}
		const auto Clear = [&](EFormationRole Role, double Yaw)
		{
			const float Wanted = static_cast<float>(NormYaw(Yaw));
			return static_cast<float>(NormYaw(FormationSectorRules::ClearLook(Walls, PosOf(Role), Wanted, Wanted, Config)));
		};
		for (const FFormationSlot& Slot : Slots)
		{
			FFormationApproachLook Look;
			Look.Role = Slot.Role;
			const TPair<EFormationRole, int32>* Flank = State.Flanks.FindByPredicate([&Slot](const TPair<EFormationRole, int32>& Each) { return Each.Key == Slot.Role; });
			if (State.bDone)
			{
				Look.Duty = EFormationApproachDuty::Layout;
				Look.Yaw = Slot.Yaw;
			}
			else if (Slot.Role == State.Runner)
			{
				if (Arrived(Slot))
				{
					Look.Duty = EFormationApproachDuty::Along;
					Look.Yaw = Clear(Slot.Role, Normal + State.AlongSide * 90.0);
				}
				else
				{
					Look.Duty = EFormationApproachDuty::Run;
				}
			}
			else if (State.Rear.IsSet() && Slot.Role == State.Rear.GetValue())
			{
				Look.Duty = EFormationApproachDuty::Rear;
				Look.Yaw = Clear(Slot.Role, Normal);
			}
			else if (Flank && !Arrived(Slot))
			{
				Look.Duty = EFormationApproachDuty::Flank;
				Look.Yaw = Clear(Slot.Role, Normal + Flank->Value * 90.0);
			}
			else
			{
				Look.Duty = EFormationApproachDuty::Layout;
				Look.Yaw = Slot.Yaw;
			}
			Result.Looks.Add(Look);
		}
		Result.State = State;
		return Result;
	}

	void BreakRoomTie(float& RoomLeft, float& RoomRight, float TravelLeft, float TravelRight)
	{
		if (RoomLeft == RoomRight && TravelLeft < TravelRight)
		{
			RoomLeft += 1.f;
		}
	}
}
