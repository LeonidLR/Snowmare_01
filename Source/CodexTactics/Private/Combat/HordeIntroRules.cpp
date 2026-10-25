#include "Combat/HordeIntroRules.h"

#include "Camera/TacticalCameraRules.h"
#include "HAL/IConsoleManager.h"

namespace
{
	TAutoConsoleVariable<int32> CVarHordeIntroSuddenStart(TEXT("Codex.HordeIntro.SuddenStart"), -1,
		TEXT("Horde waves: -1 = the data decides (default: sudden), 0 = old start (cutscene + preparation countdown), 1 = always sudden."));
	TAutoConsoleVariable<int32> CVarHordeIntroEnabled(TEXT("Codex.HordeIntro.Enabled"), -1,
		TEXT("Horde start camera flyover: -1 = default (on), 0 = off (the free tactical pause still opens at the trigger), 1 = on."));
	TAutoConsoleVariable<float> CVarHordeIntroPreDilation(TEXT("Codex.HordeIntro.PreTriggerTimeDilation"), -1.f,
		TEXT("Horde start: world time scale between the spawn and the flyover (<0 = default 1, real time)."));
	TAutoConsoleVariable<float> CVarHordeIntroMassFraction(TEXT("Codex.HordeIntro.MassMovingFraction"), -1.f,
		TEXT("Horde start: share of the live enemies that must run at the squad to trigger the flyover (<0 = default 0.6)."));
	TAutoConsoleVariable<float> CVarHordeIntroMassSpeed(TEXT("Codex.HordeIntro.MassMinSpeed"), -1.f,
		TEXT("Horde start: running speed towards the squad, cm/s (<0 = default 250)."));
	TAutoConsoleVariable<float> CVarHordeIntroMassHold(TEXT("Codex.HordeIntro.MassHoldSeconds"), -1.f,
		TEXT("Horde start: the mass must run this long before the trigger, game s (<0 = default 0.4)."));
	TAutoConsoleVariable<float> CVarHordeIntroMinDelay(TEXT("Codex.HordeIntro.MinDelaySeconds"), -1.f,
		TEXT("Horde start: earliest flyover after the spawn, game s (<0 = default 2.5)."));
	TAutoConsoleVariable<float> CVarHordeIntroMaxWait(TEXT("Codex.HordeIntro.MaxWaitSeconds"), -1.f,
		TEXT("Horde start: latest flyover after the spawn (fallback), game s (<0 = default 6)."));
	TAutoConsoleVariable<float> CVarHordeIntroFlyoverDilation(TEXT("Codex.HordeIntro.FlyoverTimeDilation"), -1.f,
		TEXT("Horde flyover: world time scale while the camera flies over the horde (<0 = default 0.2)."));
	TAutoConsoleVariable<float> CVarHordeIntroWide(TEXT("Codex.HordeIntro.WideShotSeconds"), -1.f,
		TEXT("Horde flyover: real seconds of the wide shot before the per-type shots (<0 = default 3)."));
	TAutoConsoleVariable<float> CVarHordeIntroType(TEXT("Codex.HordeIntro.TypeShotSeconds"), -1.f,
		TEXT("Horde flyover: real seconds of each per-type shot (<0 = default 2.5)."));
	TAutoConsoleVariable<float> CVarHordeIntroBlend(TEXT("Codex.HordeIntro.ShotBlendSeconds"), -1.f,
		TEXT("Horde flyover: camera glide into each shot, real s (<0 = default 0.7)."));
	TAutoConsoleVariable<float> CVarHordeIntroReturn(TEXT("Codex.HordeIntro.ReturnBlendSeconds"), -1.f,
		TEXT("Horde flyover: camera glide back to the leader, real s (<0 = default 1)."));
	TAutoConsoleVariable<float> CVarHordeIntroTypeDistance(TEXT("Codex.HordeIntro.TypeShotDistance"), -1.f,
		TEXT("Horde flyover: camera distance of a per-type tracking shot, cm (<0 = default 700)."));
	TAutoConsoleVariable<float> CVarHordeIntroWideMin(TEXT("Codex.HordeIntro.WideMinDistance"), -1.f,
		TEXT("Horde flyover: shortest wide-shot distance, cm (<0 = default 2000)."));
	TAutoConsoleVariable<float> CVarHordeIntroWideMax(TEXT("Codex.HordeIntro.WideMaxDistance"), -1.f,
		TEXT("Horde flyover: longest wide-shot distance, cm (<0 = default 4000)."));
	TAutoConsoleVariable<float> CVarHordeIntroWideBias(TEXT("Codex.HordeIntro.WideFocusBias"), -1.f,
		TEXT("Horde flyover: wide-shot focus between the squad (0) and the running mass (1) (<0 = default 0.8)."));
	TAutoConsoleVariable<float> CVarHordeIntroSide(TEXT("Codex.HordeIntro.ViewSideDegrees"), -1000.f,
		TEXT("Horde flyover: the shots look at the horde from the squad's side, turned this much aside, deg (default 25)."));
	TAutoConsoleVariable<float> CVarHordeIntroAlert(TEXT("Codex.HordeIntro.AlertSeconds"), -1.f,
		TEXT("Horde start: shortest time the «HORDE INCOMING» alert stays, real s (<0 = default 3)."));

	void HordeIntroOverride(float& Value, const TAutoConsoleVariable<float>& CVar)
	{
		const float Override = CVar.GetValueOnGameThread();
		if (Override >= 0.f)
		{
			Value = Override;
		}
	}

	FVector HordeIntroFlat(const FVector& Vector)
	{
		return FVector(Vector.X, Vector.Y, 0.f);
	}
}

FHordeIntroConfig HordeIntroRules::GetConfig()
{
	FHordeIntroConfig Config;
	const int32 Enabled = CVarHordeIntroEnabled.GetValueOnGameThread();
	if (Enabled >= 0)
	{
		Config.bEnabled = Enabled > 0;
	}
	HordeIntroOverride(Config.PreTriggerTimeDilation, CVarHordeIntroPreDilation);
	HordeIntroOverride(Config.MassMovingFraction, CVarHordeIntroMassFraction);
	HordeIntroOverride(Config.MassMinSpeed, CVarHordeIntroMassSpeed);
	HordeIntroOverride(Config.MassHoldSeconds, CVarHordeIntroMassHold);
	HordeIntroOverride(Config.MinDelaySeconds, CVarHordeIntroMinDelay);
	HordeIntroOverride(Config.MaxWaitSeconds, CVarHordeIntroMaxWait);
	HordeIntroOverride(Config.FlyoverTimeDilation, CVarHordeIntroFlyoverDilation);
	HordeIntroOverride(Config.WideShotSeconds, CVarHordeIntroWide);
	HordeIntroOverride(Config.TypeShotSeconds, CVarHordeIntroType);
	HordeIntroOverride(Config.ShotBlendSeconds, CVarHordeIntroBlend);
	HordeIntroOverride(Config.ReturnBlendSeconds, CVarHordeIntroReturn);
	HordeIntroOverride(Config.TypeShotDistance, CVarHordeIntroTypeDistance);
	HordeIntroOverride(Config.WideMinDistance, CVarHordeIntroWideMin);
	HordeIntroOverride(Config.WideMaxDistance, CVarHordeIntroWideMax);
	HordeIntroOverride(Config.WideFocusBias, CVarHordeIntroWideBias);
	HordeIntroOverride(Config.AlertSeconds, CVarHordeIntroAlert);
	if (const float Side = CVarHordeIntroSide.GetValueOnGameThread(); Side > -360.f)
	{
		Config.ViewSideDegrees = Side;
	}
	Config.PreTriggerTimeDilation = FMath::Clamp(Config.PreTriggerTimeDilation, 0.05f, 1.f);
	Config.FlyoverTimeDilation = FMath::Clamp(Config.FlyoverTimeDilation, 0.01f, 1.f);
	Config.MassMovingFraction = FMath::Clamp(Config.MassMovingFraction, 0.f, 1.f);
	Config.MaxWaitSeconds = FMath::Max(Config.MaxWaitSeconds, Config.MinDelaySeconds);
	Config.WideFocusBias = FMath::Clamp(Config.WideFocusBias, 0.f, 1.f);
	Config.WideMaxDistance = FMath::Max(Config.WideMaxDistance, Config.WideMinDistance);
	return Config;
}

int32 HordeIntroRules::GetSuddenStartOverride()
{
	return FMath::Clamp(CVarHordeIntroSuddenStart.GetValueOnGameThread(), -1, 1);
}

TArray<EEnemyArchetype> HordeIntroRules::UniqueTypesInOrder(const TArray<EEnemyArchetype>& Spawned)
{
	TArray<EEnemyArchetype> Types;
	for (const EEnemyArchetype Type : Spawned)
	{
		Types.AddUnique(Type);
	}
	return Types;
}

TArray<FHordeIntroShot> HordeIntroRules::BuildShotList(const TArray<EEnemyArchetype>& PresentTypes,
	const FHordeIntroMemory& Shown, const FHordeIntroConfig& Config)
{
	TArray<FHordeIntroShot> Shots;
	if (!Config.bEnabled || PresentTypes.IsEmpty())
	{
		return Shots;
	}
	TArray<EEnemyArchetype> NewTypes;
	for (const EEnemyArchetype Type : UniqueTypesInOrder(PresentTypes))
	{
		if (!Shown.ShownTypes.Contains(Type) && NewTypes.Num() < FMath::Max(0, Config.MaxTypeShots))
		{
			NewTypes.Add(Type);
		}
	}
	// Game designer 2026-10-25: the wide shot of the running horde only for the level's first flyover; later waves fly only to a
	// type never shown on this level - and a wave of known types has no flyover at all.
	if (Shown.bWideShotShown && NewTypes.IsEmpty())
	{
		return Shots;
	}
	if (!Shown.bWideShotShown && Config.WideShotSeconds > 0.f)
	{
		Shots.Add({ EHordeIntroShotKind::Wide, EEnemyArchetype::Base, Config.WideShotSeconds });
	}
	if (Shots.IsEmpty() && NewTypes.IsEmpty())
	{
		return Shots; // the first flyover with the wide shot switched off and nothing new: nothing to show
	}
	for (const EEnemyArchetype Type : NewTypes)
	{
		Shots.Add({ EHordeIntroShotKind::Type, Type, FMath::Max(0.1f, Config.TypeShotSeconds) });
	}
	Shots.Add({ EHordeIntroShotKind::Return, EEnemyArchetype::Base, FMath::Max(0.1f, Config.ReturnBlendSeconds) });
	return Shots;
}

void HordeIntroRules::RememberShots(FHordeIntroMemory& Memory, const TArray<FHordeIntroShot>& Shots)
{
	for (const FHordeIntroShot& Shot : Shots)
	{
		if (Shot.Kind == EHordeIntroShotKind::Wide)
		{
			Memory.bWideShotShown = true;
		}
		else if (Shot.Kind == EHordeIntroShotKind::Type)
		{
			Memory.ShownTypes.Add(Shot.Archetype);
		}
	}
}

float HordeIntroRules::GetShotsSeconds(const TArray<FHordeIntroShot>& Shots)
{
	float Total = 0.f;
	for (const FHordeIntroShot& Shot : Shots)
	{
		Total += Shot.Seconds;
	}
	return Total;
}

FHordeIntroCursor HordeIntroRules::Evaluate(const TArray<FHordeIntroShot>& Shots, float RealSecondsSinceStart)
{
	FHordeIntroCursor Cursor;
	float Time = FMath::Max(0.f, RealSecondsSinceStart);
	for (int32 Index = 0; Index < Shots.Num(); ++Index)
	{
		if (Time < Shots[Index].Seconds)
		{
			Cursor.ShotIndex = Index;
			Cursor.ShotSeconds = Time;
			return Cursor;
		}
		Time -= Shots[Index].Seconds;
	}
	Cursor.bFinished = true;
	return Cursor;
}

bool HordeIntroRules::IsRunningAtSquad(const FHordeMover& Mover, const FVector& SquadCentre, float MinSpeed)
{
	const FVector Velocity = HordeIntroFlat(Mover.Velocity);
	const FVector ToSquad = HordeIntroFlat(SquadCentre - Mover.Location);
	if (Velocity.Size() < MinSpeed || ToSquad.IsNearlyZero())
	{
		return false;
	}
	return FVector::DotProduct(Velocity.GetSafeNormal(), ToSquad.GetSafeNormal()) >= 0.5f; // within 60 deg
}

float HordeIntroRules::RunningFraction(const TArray<FHordeMover>& Movers, const FVector& SquadCentre, float MinSpeed)
{
	if (Movers.IsEmpty())
	{
		return 0.f;
	}
	int32 Running = 0;
	for (const FHordeMover& Mover : Movers)
	{
		Running += IsRunningAtSquad(Mover, SquadCentre, MinSpeed) ? 1 : 0;
	}
	return static_cast<float>(Running) / Movers.Num();
}

bool HordeIntroRules::IsMassFormed(float MassRunningSeconds, const FHordeIntroConfig& Config)
{
	return MassRunningSeconds >= Config.MassHoldSeconds;
}

bool HordeIntroRules::ShouldTrigger(float SecondsSinceSpawn, bool bMassFormed, const FHordeIntroConfig& Config)
{
	if (SecondsSinceSpawn >= Config.MaxWaitSeconds)
	{
		return true;
	}
	return SecondsSinceSpawn >= Config.MinDelaySeconds && bMassFormed;
}

bool HordeIntroRules::ShouldTriggerSpawned(float SecondsSinceSpawn, bool bMassFormed, bool bAllSpawned, int32 LiveEnemies,
	const FHordeIntroConfig& Config)
{
	const bool bGraceOver = SecondsSinceSpawn >= Config.MaxWaitSeconds + FMath::Max(0.f, Config.SpawnGraceSeconds);
	if ((!bAllSpawned || LiveEnemies <= 0) && !bGraceOver)
	{
		return false;
	}
	return ShouldTrigger(SecondsSinceSpawn, bMassFormed, Config);
}

TArray<int32> HordeIntroRules::MainCluster(const TArray<FVector>& Points, float RadiusCm)
{
	TArray<int32> Result;
	if (Points.IsEmpty())
	{
		return Result;
	}
	const float RadiusSq = FMath::Square(FMath::Max(1.f, RadiusCm));
	int32 Best = 0;
	int32 BestCount = -1;
	for (int32 Index = 0; Index < Points.Num(); ++Index)
	{
		int32 Count = 0;
		for (const FVector& Other : Points)
		{
			Count += FVector::DistSquared2D(Points[Index], Other) <= RadiusSq ? 1 : 0;
		}
		if (Count > BestCount)
		{
			BestCount = Count;
			Best = Index;
		}
	}
	TArray<int32> Seed;
	for (int32 Index = 0; Index < Points.Num(); ++Index)
	{
		if (FVector::DistSquared2D(Points[Best], Points[Index]) <= RadiusSq)
		{
			Seed.Add(Index);
		}
	}
	const FVector Centre = CentreOf(Points, Seed);
	for (int32 Index = 0; Index < Points.Num(); ++Index)
	{
		if (FVector::DistSquared2D(Centre, Points[Index]) <= RadiusSq)
		{
			Result.Add(Index);
		}
	}
	return Result.IsEmpty() ? Seed : Result;
}

FVector HordeIntroRules::CentreOf(const TArray<FVector>& Points, const TArray<int32>& Indices)
{
	FVector Sum = FVector::ZeroVector;
	int32 Count = 0;
	if (Indices.IsEmpty())
	{
		for (const FVector& Point : Points)
		{
			Sum += Point;
			++Count;
		}
	}
	else
	{
		for (const int32 Index : Indices)
		{
			if (Points.IsValidIndex(Index))
			{
				Sum += Points[Index];
				++Count;
			}
		}
	}
	return Count > 0 ? Sum / Count : FVector::ZeroVector;
}

FHordeFrameView HordeIntroRules::ViewAt(const FVector& Focus, float YawDeg, float PitchDeg, float Distance, float VerticalFovDeg, float Aspect)
{
	FHordeFrameView View;
	View.Location = Focus + TacticalCameraRules::ComputeViewOffset(YawDeg, PitchDeg, Distance);
	View.Rotation = FRotator(PitchDeg, YawDeg, 0.f);
	View.VerticalFovDeg = VerticalFovDeg;
	View.Aspect = Aspect;
	return View;
}

FHordeFramePoint HordeIntroRules::Project(const FHordeFrameView& View, const FVector& Point)
{
	FHordeFramePoint Out;
	const FVector Local = View.Rotation.UnrotateVector(Point - View.Location);
	Out.Depth = Local.X;
	if (Local.X <= 1.f)
	{
		Out.X = Out.Y = 10.f; // behind the camera: never in the frame
		return Out;
	}
	const float TanV = FMath::Tan(FMath::DegreesToRadians(FMath::Clamp(View.VerticalFovDeg, 1.f, 170.f) * 0.5f));
	const float TanH = TanV * FMath::Max(0.1f, View.Aspect);
	Out.X = Local.Y / (Local.X * TanH);
	Out.Y = Local.Z / (Local.X * TanV);
	return Out;
}

bool HordeIntroRules::IsInFrame(const FHordeFrameView& View, const FVector& Point, float Margin)
{
	const FHordeFramePoint Projected = Project(View, Point);
	const float Limit = 1.f - FMath::Clamp(Margin, 0.f, 0.45f) * 2.f;
	return Projected.Depth > 1.f && FMath::Abs(Projected.X) <= Limit && FMath::Abs(Projected.Y) <= Limit;
}

float HordeIntroRules::ScreenHeightFraction(const FHordeFrameView& View, const FVector& Feet, float HeightCm)
{
	const FHordeFramePoint Bottom = Project(View, Feet);
	const FHordeFramePoint Top = Project(View, Feet + FVector(0.f, 0.f, HeightCm));
	if (Bottom.Depth <= 1.f || Top.Depth <= 1.f)
	{
		return 0.f;
	}
	return FMath::Abs(Top.Y - Bottom.Y) * 0.5f; // the frame spans -1..1
}

namespace
{
	/** Every Feet point and its head (FramedBodyHeightCm above) inside the view with the margin. */
	bool HordeIntroHoldsAll(const FHordeFrameView& View, const TArray<FVector>& Feet, const FHordeIntroConfig& Config)
	{
		for (const FVector& Point : Feet)
		{
			if (!HordeIntroRules::IsInFrame(View, Point, Config.WideFrameMargin)
				|| !HordeIntroRules::IsInFrame(View, Point + FVector(0.f, 0.f, Config.FramedBodyHeightCm), Config.WideFrameMargin))
			{
				return false;
			}
		}
		return true;
	}
}

float HordeIntroRules::FrameDistance(const FVector& Focus, const TArray<FVector>& Feet, float YawDeg, float PitchDeg, float VerticalFovDeg,
	float Aspect, const FHordeIntroConfig& Config)
{
	const float MinDistance = FMath::Max(100.f, Config.WideMinDistance);
	const float MaxDistance = FMath::Max(MinDistance, Config.WideMaxDistance);
	auto Holds = [&](float Distance)
	{
		return HordeIntroHoldsAll(ViewAt(Focus, YawDeg, PitchDeg, Distance, VerticalFovDeg, Aspect), Feet, Config);
	};
	if (Holds(MinDistance))
	{
		return MinDistance;
	}
	if (!Holds(MaxDistance))
	{
		return MaxDistance;
	}
	float Low = MinDistance;
	float High = MaxDistance;
	for (int32 Step = 0; Step < 24 && High - Low > 10.f; ++Step)
	{
		const float Mid = (Low + High) * 0.5f;
		if (Holds(Mid))
		{
			High = Mid;
		}
		else
		{
			Low = Mid;
		}
	}
	return High;
}

HordeIntroRules::FWideFrame HordeIntroRules::FrameWide(const FVector& SquadCentre, const TArray<FVector>& ClusterFeet, float YawDeg,
	float PitchDeg, float VerticalFovDeg, float Aspect, const FHordeIntroConfig& Config)
{
	FWideFrame Frame;
	if (ClusterFeet.IsEmpty())
	{
		Frame.Focus = SquadCentre;
		Frame.Distance = FMath::Max(100.f, Config.WideMinDistance);
		return Frame;
	}
	const FVector Centre = CentreOf(ClusterFeet, TArray<int32>());
	const FVector Biased = WideFocus(SquadCentre, Centre, Config);
	const float BiasedDistance = FrameDistance(Biased, ClusterFeet, YawDeg, PitchDeg, VerticalFovDeg, Aspect, Config);
	if (HordeIntroHoldsAll(ViewAt(Biased, YawDeg, PitchDeg, BiasedDistance, VerticalFovDeg, Aspect), ClusterFeet, Config))
	{
		Frame.Focus = Biased;
		Frame.Distance = BiasedDistance;
		Frame.bBiased = true;
		return Frame;
	}
	Frame.Focus = Centre;
	Frame.Distance = FrameDistance(Centre, ClusterFeet, YawDeg, PitchDeg, VerticalFovDeg, Aspect, Config);
	return Frame;
}

FVector HordeIntroRules::WideFocus(const FVector& SquadCentre, const FVector& HordeCentre, const FHordeIntroConfig& Config)
{
	return FMath::Lerp(SquadCentre, HordeCentre, FMath::Clamp(Config.WideFocusBias, 0.f, 1.f));
}

float HordeIntroRules::WideDistance(const FVector& Focus, const TArray<FVector>& Points, const FHordeIntroConfig& Config)
{
	float Span = 0.f;
	for (const FVector& Point : Points)
	{
		Span = FMath::Max(Span, FVector::Dist2D(Focus, Point));
	}
	return FMath::Clamp(Span * Config.WideSpanScale + Config.WidePaddingCm, Config.WideMinDistance,
		FMath::Max(Config.WideMinDistance, Config.WideMaxDistance));
}

float HordeIntroRules::ViewYawAt(const FVector& SquadCentre, const FVector& Target, float SideDegrees)
{
	const FVector Direction = HordeIntroFlat(Target - SquadCentre);
	const float Yaw = Direction.IsNearlyZero() ? 0.f : Direction.Rotation().Yaw;
	return FRotator::NormalizeAxis(Yaw + SideDegrees);
}

int32 HordeIntroRules::PickRunningRepresentative(const TArray<FHordeMover>& Candidates, const FVector& SquadCentre, float MinSpeed)
{
	int32 BestRunner = INDEX_NONE;
	int32 BestMover = INDEX_NONE;
	int32 BestAny = INDEX_NONE;
	float RunnerDistance = TNumericLimits<float>::Max();
	float MoverDistance = TNumericLimits<float>::Max();
	float AnyDistance = TNumericLimits<float>::Max();
	for (int32 Index = 0; Index < Candidates.Num(); ++Index)
	{
		const float Distance = FVector::DistSquared2D(Candidates[Index].Location, SquadCentre);
		if (Distance < AnyDistance)
		{
			AnyDistance = Distance;
			BestAny = Index;
		}
		if (Distance < MoverDistance && HordeIntroFlat(Candidates[Index].Velocity).Size() > MinSpeed * 0.2f)
		{
			MoverDistance = Distance;
			BestMover = Index;
		}
		if (Distance < RunnerDistance && IsRunningAtSquad(Candidates[Index], SquadCentre, MinSpeed))
		{
			RunnerDistance = Distance;
			BestRunner = Index;
		}
	}
	return BestRunner != INDEX_NONE ? BestRunner : (BestMover != INDEX_NONE ? BestMover : BestAny);
}

float HordeIntroRules::AlertAlpha(float Seconds, float ShowSeconds, const FHordeIntroConfig& Config)
{
	if (Seconds < 0.f || Seconds > ShowSeconds + Config.AlertFadeSeconds)
	{
		return 0.f;
	}
	const float Fade = FMath::Max(0.01f, Config.AlertFadeSeconds);
	const float In = FMath::Clamp(Seconds / Fade, 0.f, 1.f);
	const float Out = FMath::Clamp((ShowSeconds + Fade - Seconds) / Fade, 0.f, 1.f);
	return FMath::Min(In, Out);
}

FString HordeIntroRules::ShotLabel(const FHordeIntroShot& Shot)
{
	switch (Shot.Kind)
	{
	case EHordeIntroShotKind::Wide:
		return TEXT("wide");
	case EHordeIntroShotKind::Type:
	{
		const UEnum* Enum = StaticEnum<EEnemyArchetype>();
		return FString::Printf(TEXT("type:%s"), Enum ? *Enum->GetNameStringByValue(static_cast<int64>(Shot.Archetype)) : TEXT("?"));
	}
	default:
		return TEXT("return");
	}
}
