#include "Combat/HordeIntroRules.h"

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
	TAutoConsoleVariable<float> CVarHordeIntroRepeatWide(TEXT("Codex.HordeIntro.RepeatWideShotSeconds"), -1.f,
		TEXT("Horde flyover: wide shot of a wave with no new enemy types, real s (<0 = default 2.5; 0 = no flyover then)."));
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
	HordeIntroOverride(Config.RepeatWideShotSeconds, CVarHordeIntroRepeatWide);
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
	const TSet<EEnemyArchetype>& AlreadyShown, const FHordeIntroConfig& Config)
{
	TArray<FHordeIntroShot> Shots;
	if (!Config.bEnabled || PresentTypes.IsEmpty())
	{
		return Shots;
	}
	TArray<EEnemyArchetype> NewTypes;
	for (const EEnemyArchetype Type : UniqueTypesInOrder(PresentTypes))
	{
		if (!AlreadyShown.Contains(Type) && NewTypes.Num() < FMath::Max(0, Config.MaxTypeShots))
		{
			NewTypes.Add(Type);
		}
	}
	const float WideSeconds = NewTypes.IsEmpty() ? Config.RepeatWideShotSeconds : Config.WideShotSeconds;
	if (WideSeconds <= 0.f && NewTypes.IsEmpty())
	{
		return Shots; // a wave of known types without a repeat wide shot: no flyover
	}
	if (WideSeconds > 0.f)
	{
		Shots.Add({ EHordeIntroShotKind::Wide, EEnemyArchetype::Base, WideSeconds });
	}
	for (const EEnemyArchetype Type : NewTypes)
	{
		Shots.Add({ EHordeIntroShotKind::Type, Type, FMath::Max(0.1f, Config.TypeShotSeconds) });
	}
	Shots.Add({ EHordeIntroShotKind::Return, EEnemyArchetype::Base, FMath::Max(0.1f, Config.ReturnBlendSeconds) });
	return Shots;
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
