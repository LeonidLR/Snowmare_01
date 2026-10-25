#include "Environment/SnowInteractionRules.h"

#include "Core/PlayerCommands.h"
#include "Data/CombatTypes.h"

namespace SnowInteractionRules
{
	int32 ResolveResolution(int32 Requested, int32 EffectsQuality)
	{
		if (Requested == 1024 || Requested == 2048 || Requested == 4096)
		{
			return Requested;
		}
		return EffectsQuality <= 0 ? 1024 : 2048;
	}

	double GetWindowSizeCm(int32 Resolution)
	{
		return Resolution <= 1024 ? 4800. : 6400.;
	}

	double GetSnapStepCm(int32 Resolution, double WindowSizeCm)
	{
		return Resolution > 0 ? SnapTexels * WindowSizeCm / Resolution : WindowSizeCm;
	}

	FVector2D ComputeWindowCentre(const FVector& Focus, float ViewYawDeg, float LeadCm)
	{
		const double Yaw = FMath::DegreesToRadians(static_cast<double>(ViewYawDeg));
		return FVector2D(Focus.X + FMath::Cos(Yaw) * LeadCm, Focus.Y + FMath::Sin(Yaw) * LeadCm);
	}

	FVector2D SnapWindowOrigin(const FVector2D& Centre, double WindowSizeCm, double StepCm)
	{
		const FVector2D Corner = Centre - FVector2D(WindowSizeCm * 0.5, WindowSizeCm * 0.5);
		if (StepCm <= 0.)
		{
			return Corner;
		}
		return FVector2D(FMath::RoundToDouble(Corner.X / StepCm) * StepCm, FMath::RoundToDouble(Corner.Y / StepCm) * StepCm);
	}

	FVector2D UpdateWindowOrigin(const FVector2D& Current, bool bCurrentValid, const FVector2D& Centre, double WindowSizeCm, double StepCm,
		double KeepSteps)
	{
		if (bCurrentValid && StepCm > 0.)
		{
			const FVector2D Offset = Centre - (Current + FVector2D(WindowSizeCm * 0.5, WindowSizeCm * 0.5));
			if (FMath::Abs(Offset.X) <= StepCm * KeepSteps && FMath::Abs(Offset.Y) <= StepCm * KeepSteps)
			{
				return Current;
			}
		}
		return SnapWindowOrigin(Centre, WindowSizeCm, StepCm);
	}

	FIntPoint ComputeShiftTexels(const FVector2D& OldOrigin, const FVector2D& NewOrigin, int32 Resolution, double WindowSizeCm)
	{
		const double Texel = Resolution > 0 ? WindowSizeCm / Resolution : 1.;
		const FVector2D Delta = (NewOrigin - OldOrigin) / Texel;
		return FIntPoint(static_cast<int32>(FMath::RoundToDouble(Delta.X)), static_cast<int32>(FMath::RoundToDouble(Delta.Y)));
	}

	FVector2D WorldToUV(const FVector2D& World, const FVector2D& Origin, double WindowSizeCm)
	{
		const double Inv = WindowSizeCm > 0. ? 1. / WindowSizeCm : 0.;
		return (World - Origin) * Inv;
	}

	FVector2D UVToWorld(const FVector2D& UV, const FVector2D& Origin, double WindowSizeCm)
	{
		return Origin + UV * WindowSizeCm;
	}

	FLinearColor MakeWindowParam(const FVector2D& Origin, double WindowSizeCm, bool bEnabled)
	{
		const double Inv = WindowSizeCm > 0. ? 1. / WindowSizeCm : 0.;
		return FLinearColor(static_cast<float>(Origin.X), static_cast<float>(Origin.Y), static_cast<float>(Inv), bEnabled ? 1.f : 0.f);
	}

	bool IsInsideWindow(const FVector2D& World, const FVector2D& Origin, double WindowSizeCm, double MarginCm)
	{
		return World.X >= Origin.X + MarginCm && World.Y >= Origin.Y + MarginCm
			&& World.X <= Origin.X + WindowSizeCm - MarginCm && World.Y <= Origin.Y + WindowSizeCm - MarginCm;
	}

	float YawFromDelta(const FVector2D& Delta, float FallbackYaw, float MinLengthCm)
	{
		if (Delta.SizeSquared() < FMath::Square(static_cast<double>(MinLengthCm)))
		{
			return FallbackYaw;
		}
		return static_cast<float>(FMath::RadiansToDegrees(FMath::Atan2(Delta.Y, Delta.X)));
	}

	float HashUnit(const FVector2D& Point, uint32 Salt)
	{
		uint32 H = HashCombine(GetTypeHash(FMath::FloorToInt32(Point.X)), GetTypeHash(FMath::FloorToInt32(Point.Y)));
		H ^= Salt * 0x9E3779B9u;
		// Murmur3 finaliser: neighbouring centimetres give unrelated values.
		H ^= H >> 16;
		H *= 0x85EBCA6Bu;
		H ^= H >> 13;
		H *= 0xC2B2AE35u;
		H ^= H >> 16;
		return static_cast<float>(H & 0xFFFFFFu) / static_cast<float>(0xFFFFFFu);
	}

	FSnowStamp MakePrint(const FSnowStampSpec& Spec, const FVector2D& Position, float YawDeg, float Scale)
	{
		const float Jitter = (HashUnit(Position, 1u) * 2.f - 1.f) * Spec.RandomYawDeg;
		const float Size = Scale * (1.f + (HashUnit(Position, 2u) * 2.f - 1.f) * Spec.RandomScale);
		FSnowStamp Stamp;
		Stamp.Centre = Position;
		Stamp.YawDeg = (Spec.bAlignToMotion ? YawDeg : 0.f) + Jitter;
		Stamp.LengthCm = Spec.LengthCm * Size;
		Stamp.WidthCm = Spec.WidthCm * Size;
		Stamp.Depth = Spec.Depth;
		Stamp.Berm = Spec.Berm;
		Stamp.Shape = Spec.Shape;
		return Stamp;
	}

	int32 AdvanceStride(FSnowStrideState& State, const FVector2D& Position, const FSnowStampSpec& Spec, float MaxJumpCm,
		TArray<FSnowStamp>& Out, float Scale)
	{
		const float Spacing = FMath::Max(Spec.SpacingCm * Scale, 1.f);
		const FVector2D Delta = Position - State.Last;
		const double Length = Delta.Size();
		if (!State.bValid || Length > MaxJumpCm)
		{
			// First sight or a teleport: the first print comes half a stride later.
			State.Last = Position;
			State.Distance = Spacing * 0.5f;
			State.bValid = true;
			return 0;
		}
		if (Length <= UE_KINDA_SMALL_NUMBER)
		{
			return 0;
		}
		const FVector2D Dir = Delta / Length;
		const FVector2D Right(-Dir.Y, Dir.X);
		const float Yaw = YawFromDelta(Delta, 0.f, 0.f);
		const float Side = Spec.SideOffsetCm * Scale;
		int32 Count = 0;
		State.Distance += static_cast<float>(Length);
		while (State.Distance >= Spacing)
		{
			State.Distance -= Spacing;
			const FVector2D OnPath = State.Last + Dir * (Length - State.Distance);
			Out.Add(MakePrint(Spec, OnPath + Right * (State.bLeft ? -Side : Side), Yaw, Scale));
			State.bLeft = !State.bLeft;
			++Count;
		}
		State.Last = Position;
		return Count;
	}

	FSnowStamp MakeSegment(const FSnowStampSpec& Spec, const FVector2D& A, const FVector2D& B)
	{
		FSnowStamp Stamp;
		Stamp.Centre = (A + B) * 0.5;
		Stamp.YawDeg = YawFromDelta(B - A, 0.f, 0.f);
		Stamp.LengthCm = static_cast<float>(FVector2D::Distance(A, B));
		Stamp.WidthCm = Spec.WidthCm;
		Stamp.Depth = Spec.Depth;
		Stamp.Berm = Spec.Berm;
		Stamp.Shape = ESnowStampShape::Segment;
		return Stamp;
	}

	int32 AdvanceTrail(FSnowTrailState& State, const FVector2D& Position, const FSnowStampSpec& Spec, float MaxJumpCm, TArray<FSnowStamp>& Out)
	{
		const double Length = FVector2D::Distance(State.Last, Position);
		if (!State.bValid || Length > MaxJumpCm)
		{
			State.Last = Position;
			State.bValid = true;
			return 0;
		}
		if (Length < FMath::Max(Spec.SpacingCm, 1.f))
		{
			return 0;
		}
		Out.Add(MakeSegment(Spec, State.Last, Position));
		State.Last = Position;
		return 1;
	}

	void BuildStampQuad(const FSnowStamp& Stamp, FVector2D OutCorners[4])
	{
		const double Yaw = FMath::DegreesToRadians(static_cast<double>(Stamp.YawDeg));
		const FVector2D Forward(FMath::Cos(Yaw), FMath::Sin(Yaw));
		const FVector2D Right(-Forward.Y, Forward.X);
		const double HalfLength = Stamp.Shape == ESnowStampShape::Segment ? Stamp.LengthCm * 0.5 : Stamp.LengthCm * 0.5 / PrintFill;
		const double HalfWidth = Stamp.WidthCm * 0.5 / PrintFill;
		const FVector2D Along = Forward * HalfLength;
		const FVector2D Across = Right * HalfWidth;
		OutCorners[0] = Stamp.Centre - Along - Across;
		OutCorners[1] = Stamp.Centre + Along - Across;
		OutCorners[2] = Stamp.Centre + Along + Across;
		OutCorners[3] = Stamp.Centre - Along + Across;
	}

	int32 CapStamps(TArray<FSnowStamp>& Stamps, const FVector2D& Focus, int32 Max)
	{
		const int32 Count = Stamps.Num();
		Max = FMath::Max(Max, 0);
		if (Count <= Max)
		{
			return 0;
		}
		TArray<TPair<double, int32>> Order;
		Order.Reserve(Count);
		for (int32 Index = 0; Index < Count; ++Index)
		{
			Order.Emplace(FVector2D::DistSquared(Stamps[Index].Centre, Focus), Index);
		}
		Order.Sort([](const TPair<double, int32>& A, const TPair<double, int32>& B)
		{
			return A.Key != B.Key ? A.Key < B.Key : A.Value < B.Value;
		});
		TBitArray<> Keep(false, Count);
		for (int32 Rank = 0; Rank < Max; ++Rank)
		{
			Keep[Order[Rank].Value] = true;
		}
		TArray<FSnowStamp> Kept;
		Kept.Reserve(Max);
		for (int32 Index = 0; Index < Count; ++Index)
		{
			if (Keep[Index])
			{
				Kept.Add(Stamps[Index]);
			}
		}
		Stamps = MoveTemp(Kept);
		return Count - Max;
	}

	FLinearColor EncodeStampColor(ESnowStampShape Shape, float Depth, float Berm)
	{
		const uint8 R = static_cast<uint8>(FMath::Clamp(static_cast<int32>(Shape) * 16 + 8, 0, 255));
		const uint8 G = static_cast<uint8>(FMath::RoundToInt(FMath::Clamp(Depth / MaxChannelValue, 0.f, 1.f) * 255.f));
		const uint8 B = static_cast<uint8>(FMath::RoundToInt(FMath::Clamp(Berm / MaxChannelValue, 0.f, 1.f) * 255.f));
		// FLinearColor(FColor) decodes sRGB; the canvas encodes it back with ToFColor(true): the GPU sees exactly R, G, B / 255.
		return FLinearColor(FColor(R, G, B, 255));
	}

	int32 DecodeStampShape(const FColor& Stored)
	{
		return Stored.R / 16;
	}

	float DecodeStampDepth(const FColor& Stored)
	{
		return Stored.G / 255.f * MaxChannelValue;
	}

	float DecodeStampBerm(const FColor& Stored)
	{
		return Stored.B / 255.f * MaxChannelValue;
	}

	float ComputeFadeFactor(float Dt, float BaseRate, float Boost, float Refill)
	{
		const float Rate = FMath::Max(BaseRate, 0.f) * (1.f + FMath::Max(Boost, 0.f) * FMath::Clamp(Refill, 0.f, 1.f));
		return FMath::Exp(-Rate * FMath::Max(Dt, 0.f));
	}

	bool IsFadeDue(float SinceSeconds, float FadeHz, float Rate)
	{
		return SinceSeconds >= 1.f / FMath::Max(FadeHz, 0.01f) && Rate * SinceSeconds >= MinFadeStep;
	}

	float StepFreshSnow(float Current, float IsSnowing, float Dt, float RiseSeconds, float FadeSeconds)
	{
		const float Snowing = FMath::Clamp(IsSnowing, 0.f, 1.f);
		const float Step = FMath::Max(Dt, 0.f);
		float Next = FMath::Clamp(Current, 0.f, 1.f);
		if (Snowing > 0.f)
		{
			Next = FMath::Min(1.f, Next + Snowing * Step / FMath::Max(RiseSeconds, 0.001f));
		}
		else
		{
			Next = FMath::Max(0.f, Next - Step / FMath::Max(FadeSeconds, 0.001f));
		}
		return Next;
	}

	bool IsSnowGround(ESnowGroundKind Kind, bool bTaggedSnowGround, bool bAnyGround)
	{
		switch (Kind)
		{
		case ESnowGroundKind::None:
			return false;
		case ESnowGroundKind::Landscape:
			return true;
		case ESnowGroundKind::StaticMesh:
			return bTaggedSnowGround || bAnyGround;
		default:
			return bTaggedSnowGround;
		}
	}

	EEnemyTrack GetEnemyTrack(EEnemyArchetype Archetype, const FSnowStampTable& Table, const FSnowStampSpec*& OutSpec)
	{
		switch (Archetype)
		{
		case EEnemyArchetype::FrostHound:
			OutSpec = &Table.HoundPaw;
			return EEnemyTrack::Prints;
		case EEnemyArchetype::Cutter:
			OutSpec = &Table.CutterPaw;
			return EEnemyTrack::Prints;
		case EEnemyArchetype::Brute:
			OutSpec = &Table.BruteBoot;
			return EEnemyTrack::Prints;
		case EEnemyArchetype::Frostbitten:
			OutSpec = &Table.FrostbittenDrag;
			return EEnemyTrack::Trail;
		case EEnemyArchetype::CryoDrone:
			OutSpec = nullptr;
			return EEnemyTrack::None;
		default:
			OutSpec = &Table.EnemyBoot;
			return EEnemyTrack::Prints;
		}
	}

	const FSnowStampSpec& GetEnemyImprint(EEnemyArchetype Archetype, const FSnowStampTable& Table)
	{
		switch (Archetype)
		{
		case EEnemyArchetype::FrostHound:
		case EEnemyArchetype::Cutter:
			return Table.BeastImprint;
		case EEnemyArchetype::Brute:
			return Table.BruteImprint;
		default:
			return Table.EnemyImprint;
		}
	}

	const FSnowStampSpec& GetCraterSpec(FName Kind, const FSnowStampTable& Table)
	{
		if (Kind == GameplayEvents::ExplosionKinds::Grenade || Kind == GameplayEvents::ExplosionKinds::Tripwire)
		{
			return Table.GrenadeCrater;
		}
		if (Kind == GameplayEvents::ExplosionKinds::Barrel)
		{
			return Table.BarrelCrater;
		}
		if (Kind == GameplayEvents::ExplosionKinds::Mine)
		{
			return Table.MineCrater;
		}
		if (Kind == GameplayEvents::ExplosionKinds::Cannon)
		{
			return Table.CannonCrater;
		}
		return Table.Crater;
	}

	FSnowCraterParams ComputeCrater(FName Kind, float RadiusCm, const FSnowStampTable& Table, const FSnowInteractionTuning& Tuning)
	{
		const FSnowStampSpec& Spec = GetCraterSpec(Kind, Table);
		const float MinCm = FMath::Max(Tuning.CraterMinCm, 1.f);
		const float MaxCm = FMath::Max(Tuning.CraterMaxCm, MinCm);
		const float Raw = RadiusCm > 0.f ? RadiusCm * FMath::Max(Tuning.CraterDiameterPerRadius, 0.f) : Spec.LengthCm;
		FSnowCraterParams Params;
		Params.DiameterCm = FMath::Clamp(Raw, MinCm, MaxCm);
		Params.Depth = FMath::Clamp(Spec.Depth, 0.f, MaxChannelValue);
		Params.Berm = FMath::Clamp(Spec.Berm, 0.f, MaxChannelValue);
		Params.SprayDiameterCm = Params.DiameterCm * FMath::Max(Tuning.CraterSprayScale, 1.f);
		Params.SprayBerm = FMath::Clamp(Table.CraterSpray.Berm, 0.f, MaxChannelValue);
		Params.Clods = FMath::Clamp(Tuning.CraterClods, 0, 16);
		return Params;
	}

	int32 MakeCraterStamps(const FSnowCraterParams& Params, const FSnowStampTable& Table, const FVector2D& Centre, TArray<FSnowStamp>& Out)
	{
		if (Params.DiameterCm <= 0.f)
		{
			return 0;
		}
		const int32 Before = Out.Num();
		// The bowl + its rim (round; a hashed yaw and a slight oval so two craters are never the same stencil).
		FSnowStamp Bowl;
		Bowl.Centre = Centre;
		Bowl.YawDeg = HashUnit(Centre, 11u) * 360.f;
		Bowl.LengthCm = Params.DiameterCm;
		Bowl.WidthCm = Params.DiameterCm * (0.92f + 0.16f * HashUnit(Centre, 12u));
		Bowl.Depth = Params.Depth;
		Bowl.Berm = Params.Berm;
		Bowl.Shape = ESnowStampShape::Crater;
		Out.Add(Bowl);
		// A steep-walled pit inside the bowl (the bowl alone is too gentle to shade: ~8 cm over a metre): the blast's own hole.
		FSnowStamp Pit = Bowl;
		Pit.Shape = ESnowStampShape::Body;
		Pit.YawDeg = HashUnit(Centre, 10u) * 360.f;
		Pit.LengthCm = Params.DiameterCm * CraterPitScale;
		Pit.WidthCm = Pit.LengthCm * (0.85f + 0.15f * HashUnit(Centre, 9u));
		Pit.Depth = Params.Depth * 0.6f;
		Pit.Berm = 0.f;
		Out.Add(Pit);
		// The thrown-snow ring: a Crater shape with no bowl = only its rim, farther out.
		if (Params.SprayBerm > 0.f && Params.SprayDiameterCm > Params.DiameterCm)
		{
			FSnowStamp Spray = Bowl;
			Spray.YawDeg = HashUnit(Centre, 13u) * 360.f;
			Spray.LengthCm = Params.SprayDiameterCm;
			Spray.WidthCm = Params.SprayDiameterCm * (0.85f + 0.2f * HashUnit(Centre, 14u));
			Spray.Depth = 0.f;
			Spray.Berm = Params.SprayBerm;
			Out.Add(Spray);
		}
		// Clods round the crater: evenly spaced angles with a hashed offset, hashed distances, aligned outward (thrown away from the blast).
		const float StartAngle = HashUnit(Centre, 15u) * 360.f;
		for (int32 Index = 0; Index < Params.Clods; ++Index)
		{
			const uint32 Salt = 16u + static_cast<uint32>(Index) * 3u;
			const float Angle = StartAngle + (Index + 0.6f * (HashUnit(Centre, Salt) - 0.5f)) * 360.f / Params.Clods;
			const float Distance = Params.DiameterCm * (0.6f + 0.55f * HashUnit(Centre, Salt + 1u));
			const double Radians = FMath::DegreesToRadians(static_cast<double>(Angle));
			const FVector2D Where = Centre + FVector2D(FMath::Cos(Radians), FMath::Sin(Radians)) * Distance;
			Out.Add(MakePrint(Table.CraterClod, Where, Angle));
		}
		return Out.Num() - Before;
	}

	bool ComputeShotRay(const FVector& Start, const FVector& End, float ExtendCm, float BackCm, FVector& OutFrom, FVector& OutTo)
	{
		const FVector Delta = End - Start;
		const double Length = Delta.Size();
		if (Length < 1.)
		{
			return false;
		}
		const FVector Dir = Delta / Length;
		OutFrom = End - Dir * FMath::Min(static_cast<double>(FMath::Max(BackCm, 0.f)), Length);
		OutTo = End + Dir * static_cast<double>(FMath::Max(ExtendCm, 0.f));
		return true;
	}

	FSnowStamp MakeDent(const FSnowStampSpec& Spec, const FVector& Hit, const FVector& RayDir, const FVector& Normal, float MaxStretch)
	{
		const FVector Dir = RayDir.GetSafeNormal();
		const FVector Up = Normal.GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector);
		const float Sine = static_cast<float>(FMath::Abs(FVector::DotProduct(Dir, Up)));
		const float Stretch = FMath::Clamp(1.f / FMath::Max(Sine, 0.05f), 1.f, FMath::Max(MaxStretch, 1.f));
		const FVector2D Position(Hit.X, Hit.Y);
		const float RayYaw = YawFromDelta(FVector2D(Dir.X, Dir.Y), 0.f, 0.01f);
		FSnowStamp Stamp = MakePrint(Spec, Position, RayYaw);
		Stamp.YawDeg = RayYaw; // the stretch (if any) lies along the ray
		Stamp.LengthCm *= Stretch;
		return Stamp;
	}

	int32 EnqueueShot(TArray<FSnowShot>& Queue, const FSnowShot& Shot, int32 MaxQueue)
	{
		const int32 Max = FMath::Max(MaxQueue, 1);
		int32 Dropped = 0;
		if (Queue.Num() >= Max)
		{
			Dropped = Queue.Num() - Max + 1;
			Queue.RemoveAt(0, Dropped, EAllowShrinking::No);
		}
		Queue.Add(Shot);
		return Dropped;
	}

	int32 GetShotBudget(int32 Queued, int32 MaxPerFrame)
	{
		return FMath::Max(0, FMath::Min(Queued, MaxPerFrame));
	}
}
