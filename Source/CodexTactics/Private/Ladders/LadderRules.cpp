#include "Ladders/LadderRules.h"

namespace LadderRules
{
	int32 RungCountForHeight(float Height, float Pitch)
	{
		if (Pitch < 10.f)
		{
			return 0;
		}
		return FMath::Max(1, FMath::RoundToInt(Height / Pitch));
	}

	float TopZ(int32 RungCount, float Pitch)
	{
		return FMath::Max(0, RungCount) * Pitch;
	}

	bool IsClimbable(int32 RungCount, bool bTopExit, int32 MinClimbableRungs)
	{
		return bTopExit && RungCount >= MinClimbableRungs;
	}

	FVector RungLocal(int32 K, float Pitch)
	{
		return FVector(0.f, 0.f, K * Pitch);
	}

	FVector HangLocal(const FClipContract& Clip, float Z)
	{
		return FVector(Clip.HangStandoff, Clip.HangLateral, Z);
	}

	FVector BottomLinkLocal(const FClipContract& Clip)
	{
		return FVector(Clip.HangStandoff + Clip.BottomLinkExtra, Clip.HangLateral, 0.f);
	}

	FVector TopLinkLocal(const FClipContract& Clip, int32 RungCount)
	{
		return FVector(-Clip.TopStandoff, Clip.HangLateral, TopZ(RungCount, Clip.Pitch));
	}

	FVector TopExitLocal(const FClipContract& Clip, int32 RungCount)
	{
		return FVector(-Clip.TopExitStandoff, Clip.HangLateral, TopZ(RungCount, Clip.Pitch));
	}

	int32 RailSegmentCount(float InTopZ, float SegmentLen, int32 Overshoot, float Pitch)
	{
		if (SegmentLen < 1.f || Pitch < 10.f || InTopZ <= 0.f)
		{
			return INDEX_NONE;
		}
		return FMath::CeilToInt(InTopZ / SegmentLen - KINDA_SMALL_NUMBER) + FMath::Max(0, Overshoot);
	}

	TArray<float> BracketHeights(int32 Count, float Pitch, float InTopZ)
	{
		TArray<float> Heights;
		if (Count <= 0 || InTopZ <= Pitch)
		{
			return Heights;
		}
		if (Count == 1)
		{
			Heights.Add((Pitch + InTopZ) * 0.5f);
			return Heights;
		}
		for (int32 Index = 0; Index < Count; ++Index)
		{
			Heights.Add(FMath::Lerp(Pitch, InTopZ, static_cast<float>(Index) / (Count - 1)));
		}
		return Heights;
	}

	float ClimberYaw(float LadderYaw)
	{
		return FRotator::NormalizeAxis(LadderYaw + 180.f);
	}

	bool NeedsPreTurn(float CharacterYaw, float LadderYaw, float ToleranceDeg)
	{
		return FMath::Abs(FMath::FindDeltaAngleDegrees(CharacterYaw, LadderYaw)) > ToleranceDeg;
	}

	ELadderClimbState Transition(ELadderClimbState From, ELadderClimbEvent Event)
	{
		using S = ELadderClimbState;
		using E = ELadderClimbEvent;
		if (Event == E::Abort)
		{
			return S::None;
		}
		if (Event == E::Drop)
		{
			return IsOnLadderState(From) && From != S::Dropping ? S::Dropping : From;
		}
		switch (From)
		{
		case S::None:
		case S::Queue:
			return Event == E::LinkReachedBottom ? S::AlignBottom
				: Event == E::LinkReachedTop ? S::AlignTop
				: Event == E::MustWait ? S::Queue : From;
		case S::AlignBottom:
			return Event == E::AlignDone ? S::ClimbUp : From;
		case S::AlignTop:
			return Event == E::AlignDone ? S::EnterTop : From;
		case S::EnterTop:
			return Event == E::EnterDone ? S::ClimbDown : From;
		case S::ClimbUp:
			return Event == E::TargetReached ? S::ExitTop : From;
		case S::ClimbDown:
			return Event == E::TargetReached ? S::ExitBottom : From;
		case S::ExitTop:
		case S::ExitBottom:
			return Event == E::ExitDone ? S::None : From;
		case S::Dropping:
			return Event == E::Landed ? S::None : From;
		default:
			return From;
		}
	}

	bool IsOnLadderState(ELadderClimbState State)
	{
		return State != ELadderClimbState::None && State != ELadderClimbState::Queue;
	}

	float StateTimeout(float ExpectedSeconds)
	{
		return ExpectedSeconds > 0.f ? ExpectedSeconds * 2.f + 1.f : 0.f;
	}

	FLoopPos ClimbOutStart(const FClipContract& Clip, int32 RungCount)
	{
		return FLoopPos{ RungCount - Clip.ClimbOutRungs, 0.f };
	}

	FLoopPos IntoFromTopEnd(const FClipContract& Clip, int32 RungCount)
	{
		return FLoopPos{ RungCount - Clip.IntoFromTopRungs, 0.f };
	}

	FLoopPos BottomPos(int32 TargetRung, float LoopLength)
	{
		const bool bOdd = (TargetRung % 2) != 0;
		return bOdd ? FLoopPos{ -1, LoopLength * 0.5f } : FLoopPos{ 0, 0.f };
	}

	namespace
	{
		/** Ordering along the ladder (each rung = half a loop of clip time). */
		double Phase(const FLoopPos& Pos, float LoopLength)
		{
			return Pos.Rung * (LoopLength * 0.5) + Pos.Time;
		}
	}

	bool Advance(FLoopPos& Pos, float Delta, int32 Direction, const FLoopPos& Target, float LoopLength)
	{
		if (LoopLength <= KINDA_SMALL_NUMBER)
		{
			Pos = Target;
			return true;
		}
		if (Direction >= 0)
		{
			Pos.Time += FMath::Max(0.f, Delta);
			while (Pos.Time >= LoopLength)
			{
				Pos.Time -= LoopLength;
				Pos.Rung += 2;
			}
			if (Phase(Pos, LoopLength) >= Phase(Target, LoopLength) - 1e-4)
			{
				Pos = Target;
				return true;
			}
			return false;
		}
		Pos.Time -= FMath::Max(0.f, Delta);
		while (Pos.Time < 0.f)
		{
			Pos.Time += LoopLength;
			Pos.Rung -= 2;
		}
		if (Phase(Pos, LoopLength) <= Phase(Target, LoopLength) + 1e-4)
		{
			Pos = Target;
			return true;
		}
		return false;
	}

	float HeightAt(const FLoopPos& Pos, float Fraction, float Pitch)
	{
		return Pos.Rung * Pitch + 2.f * Pitch * Fraction;
	}

	namespace
	{
		/** Grip window of one limb on the up loop, in frames of the 54-frame clip (0..53). */
		struct FGripWindow
		{
			int32 StartRung;
			float ReleaseFrame;
			float GripFrame;
		};

		// Measured (LadderClipShots, operative body, 2026-10-22): hand_l on rung +5 until frame 31, on +7 from 46; hand_r +4 until 5,
		// +6 from 23; ball_l on the floor / rung +0 until 13, +2 from 23; ball_r +1 until 33, +3 from 50. Each limb moves two rungs.
		constexpr FGripWindow GripWindows[static_cast<int32>(ELadderLimb::Count)] = {
			{ 5, 32.f, 46.f },
			{ 4, 6.f, 23.f },
			{ 0, 14.f, 23.f },
			{ 1, 34.f, 50.f },
		};
		constexpr float LastFrame = 53.f;
		/** A grip fades in / out over 5 frames (~0.17 s, eased): no foot / hand pops at the contact window edges (user report 2026-10-23). */
		constexpr float FadeFrames = 5.f;
	}

	FLimbGrip LimbGrip(ELadderLimb Limb, float NormalizedTime)
	{
		FLimbGrip Grip;
		const int32 Index = static_cast<int32>(Limb);
		if (Index < 0 || Index >= static_cast<int32>(ELadderLimb::Count))
		{
			return Grip;
		}
		const FGripWindow& Window = GripWindows[Index];
		const float Frame = FMath::Clamp(NormalizedTime, 0.f, 1.f) * LastFrame;
		const float Mid = (Window.ReleaseFrame + Window.GripFrame) * 0.5f;
		if (Frame < Mid)
		{
			Grip.RungOffset = Window.StartRung;
			Grip.Alpha = FMath::SmoothStep(0.f, 1.f, FMath::Clamp((Window.ReleaseFrame - Frame) / FadeFrames, 0.f, 1.f));
		}
		else
		{
			Grip.RungOffset = Window.StartRung + 2;
			Grip.Alpha = FMath::SmoothStep(0.f, 1.f, FMath::Clamp((Frame - (Window.GripFrame - FadeFrames)) / FadeFrames, 0.f, 1.f));
		}
		return Grip;
	}

	ELadderRefusal CanStartClimb(const FEntryContext& Context)
	{
		if (Context.bDead)
		{
			return ELadderRefusal::Dead;
		}
		if (!Context.bClimbable)
		{
			return ELadderRefusal::NotClimbable;
		}
		if (!Context.bHasClips)
		{
			return ELadderRefusal::NoClips;
		}
		if (Context.bTurnBased)
		{
			return ELadderRefusal::TurnBased;
		}
		if (Context.bMounted)
		{
			return ELadderRefusal::Mounted;
		}
		if (Context.bKnockedDown)
		{
			return ELadderRefusal::KnockedDown;
		}
		if (Context.bCarrying)
		{
			return ELadderRefusal::Carrying;
		}
		if (Context.bLeasedByOther)
		{
			return ELadderRefusal::Leased;
		}
		if (!Context.bStanding)
		{
			return ELadderRefusal::MustStand;
		}
		return ELadderRefusal::None;
	}

	bool EnemyMayClimb(bool bClassClimbs, bool bHasClips, bool bLadderAllowsEnemies)
	{
		return bClassClimbs && bHasClips && bLadderAllowsEnemies;
	}

	bool NearestEndIsTop(int32 Rung, int32 RungCount)
	{
		return Rung * 2 > RungCount;
	}

	float FallDamage(float HeightCm, const FFallDamage& Rule)
	{
		return FMath::Clamp((HeightCm - Rule.SafeHeightCm) * Rule.DamagePerCm, 0.f, Rule.MaxDamage);
	}
}
