// Coordinated multi-direction ambushes: parties, phases (flank block -> rear + front), suppressors (game designer answer 2, 2026-11-01,
// docs/design/COMBAT_LAB.md). UE-only, no Godot reference. Tests: CodexTactics.Combat.AmbushPlan.*.

#include "Combat/AmbushPlanRules.h"

EAmbushParty AmbushPlanRules::PlanParty(int32 SlotIndex)
{
	switch (SlotIndex)
	{
	case 0: return EAmbushParty::FlankLeft;
	case 1: return EAmbushParty::FlankRight;
	case 2: return EAmbushParty::Front;
	case 3: return EAmbushParty::Rear;
	default: return EAmbushParty::Single;
	}
}

const TCHAR* AmbushPlanRules::PartyName(EAmbushParty Party)
{
	switch (Party)
	{
	case EAmbushParty::FlankLeft: return TEXT("flank left");
	case EAmbushParty::FlankRight: return TEXT("flank right");
	case EAmbushParty::Front: return TEXT("front");
	case EAmbushParty::Rear: return TEXT("rear");
	default: return TEXT("single");
	}
}

const TCHAR* AmbushPlanRules::PhaseName(EAmbushPlanPhase Phase)
{
	switch (Phase)
	{
	case EAmbushPlanPhase::Holding: return TEXT("holding");
	case EAmbushPlanPhase::FlankBlock: return TEXT("flank block");
	case EAmbushPlanPhase::Assault: return TEXT("assault");
	default: return TEXT("waiting");
	}
}

bool AmbushPlanRules::IsFlank(EAmbushParty Party)
{
	return Party == EAmbushParty::FlankLeft || Party == EAmbushParty::FlankRight;
}

bool AmbushPlanRules::IsRanged(EEnemyArchetype Type)
{
	return Type == EEnemyArchetype::Spitter || Type == EEnemyArchetype::Marksman;
}

EAmbushParty AmbushPlanRules::PartyOf(int32 MemberIndex)
{
	if (MemberIndex < 4)
	{
		return PlanParty(FMath::Max(0, MemberIndex));
	}
	return (MemberIndex - 4) % 2 == 0 ? EAmbushParty::Front : EAmbushParty::Rear;
}

int32 AmbushPlanRules::PlanBodies(float Rand01, const FAmbushPlanParams& Params, int32 Alive)
{
	const int32 Low = FMath::Max(4, Params.BodiesMin);
	const int32 High = FMath::Max(Low, Params.BodiesMax);
	const int32 Size = FMath::Clamp(Low + FMath::FloorToInt(FMath::Clamp(Rand01, 0.f, 0.9999f) * static_cast<float>(High - Low + 1)), Low, High);
	const int32 Room = FMath::Max(0, Params.MaxAlive - FMath::Max(0, Alive));
	return Room < 4 ? 0 : FMath::Min(Size, Room);
}

int32 AmbushPlanRules::EnsureRanged(TArray<EEnemyArchetype>& Types, EEnemyArchetype Ranged, int32 MinRanged)
{
	int32 Have = 0;
	for (const EEnemyArchetype Type : Types)
	{
		Have += IsRanged(Type) ? 1 : 0;
	}
	if (!IsRanged(Ranged))
	{
		return 0;
	}
	int32 Swapped = 0;
	for (const EAmbushParty Party : { EAmbushParty::Front, EAmbushParty::Rear })
	{
		for (int32 Index = 0; Index < Types.Num() && Have < MinRanged; ++Index)
		{
			if (PartyOf(Index) == Party && !IsRanged(Types[Index]))
			{
				Types[Index] = Ranged;
				++Have;
				++Swapped;
			}
		}
	}
	return Swapped;
}

FVector2D AmbushPlanRules::Anchor(const FVector2D& Leader, float HeadingYawDeg, const FAmbushPlanParams& Params)
{
	const float Rad = FMath::DegreesToRadians(HeadingYawDeg);
	return Leader + FVector2D(FMath::Cos(Rad), FMath::Sin(Rad)) * Params.AnchorAheadCm;
}

FVector2D AmbushPlanRules::SlotPoint(const FVector2D& Anchor, float HeadingYawDeg, EAmbushParty Party, const FAmbushPlanParams& Params)
{
	float Turn = 0.f;
	float Offset = Params.FrontOffsetCm;
	switch (Party)
	{
	case EAmbushParty::FlankRight: Turn = 90.f; Offset = Params.FlankOffsetCm; break;
	case EAmbushParty::FlankLeft: Turn = -90.f; Offset = Params.FlankOffsetCm; break;
	case EAmbushParty::Rear: Turn = 180.f; Offset = Params.RearOffsetCm; break;
	default: break;
	}
	const float Rad = FMath::DegreesToRadians(HeadingYawDeg + Turn);
	return Anchor + FVector2D(FMath::Cos(Rad), FMath::Sin(Rad)) * Offset;
}

EAmbushParty AmbushPlanRules::SectorOf(const FVector2D& Anchor, float HeadingYawDeg, const FVector2D& Point)
{
	const FVector2D Delta = Point - Anchor;
	const float Bearing = Delta.IsNearlyZero() ? HeadingYawDeg : static_cast<float>(FMath::RadiansToDegrees(FMath::Atan2(Delta.Y, Delta.X)));
	const float Relative = FMath::FindDeltaAngleDegrees(HeadingYawDeg, Bearing);
	if (FMath::Abs(Relative) <= 45.f)
	{
		return EAmbushParty::Front;
	}
	if (FMath::Abs(Relative) >= 135.f)
	{
		return EAmbushParty::Rear;
	}
	return Relative > 0.f ? EAmbushParty::FlankRight : EAmbushParty::FlankLeft;
}

TArray<int32> AmbushPlanRules::AssignSpots(const FVector2D& Anchor, float HeadingYawDeg, const TArray<FVector2D>& Candidates,
	const FAmbushPlanParams& Params)
{
	TArray<int32> Out;
	Out.Init(INDEX_NONE, 4);
	TArray<FVector2D> Taken;
	for (int32 Slot = 0; Slot < 4; ++Slot)
	{
		const EAmbushParty Party = PlanParty(Slot);
		const FVector2D Want = SlotPoint(Anchor, HeadingYawDeg, Party, Params);
		float Best = MAX_flt;
		for (int32 Index = 0; Index < Candidates.Num(); ++Index)
		{
			const FVector2D& Point = Candidates[Index];
			if (SectorOf(Anchor, HeadingYawDeg, Point) != Party)
			{
				continue;
			}
			bool bTooClose = false;
			for (const FVector2D& Used : Taken)
			{
				bTooClose |= FVector2D::Distance(Used, Point) < Params.MinSeparationCm;
			}
			const float Distance = static_cast<float>(FVector2D::Distance(Point, Want));
			if (!bTooClose && Distance < Best)
			{
				Best = Distance;
				Out[Slot] = Index;
			}
		}
		if (Out[Slot] != INDEX_NONE)
		{
			Taken.Add(Candidates[Out[Slot]]);
		}
	}
	return Out;
}

bool AmbushPlanRules::IsComplete(const TArray<int32>& Assigned)
{
	return Assigned.Num() >= 4 && !Assigned.Contains(INDEX_NONE);
}

EAmbushPlanPhase AmbushPlanRules::NextPhase(const FAmbushPlanTick& Tick, const FAmbushPlanParams& Params)
{
	switch (Tick.Phase)
	{
	case EAmbushPlanPhase::Waiting:
	case EAmbushPlanPhase::Holding:
		if (Tick.bAnyEngaged)
		{
			return EAmbushPlanPhase::FlankBlock;
		}
		if (Tick.NearestSquadCm > Params.TriggerRangeCm)
		{
			return EAmbushPlanPhase::Waiting;
		}
		return Tick.FlanksUnwatched > 0 ? EAmbushPlanPhase::FlankBlock : EAmbushPlanPhase::Holding;
	case EAmbushPlanPhase::FlankBlock:
	{
		if (Tick.SecondsInPhase >= Params.AssaultMaxSeconds)
		{
			return EAmbushPlanPhase::Assault;
		}
		const bool bBlocked = Tick.FlankMembersAlive <= 0 || Tick.FlankMembersInPlace >= Tick.FlankMembersAlive;
		return Tick.SecondsInPhase >= Params.AssaultMinSeconds && bBlocked ? EAmbushPlanPhase::Assault : EAmbushPlanPhase::FlankBlock;
	}
	default:
		return EAmbushPlanPhase::Assault;
	}
}

bool AmbushPlanRules::Springs(EAmbushPlanPhase Phase, EAmbushParty Party, bool bRanged)
{
	switch (Phase)
	{
	case EAmbushPlanPhase::Assault:
		return true;
	case EAmbushPlanPhase::FlankBlock:
		return IsFlank(Party) || bRanged || Party == EAmbushParty::Single;
	default:
		return false;
	}
}
