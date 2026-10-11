#include "Combat/HordeIntroRules.h"

#include "HAL/IConsoleManager.h"

namespace
{
	TAutoConsoleVariable<int32> CVarHordeIntroSuddenStart(TEXT("Codex.HordeIntro.SuddenStart"), -1,
		TEXT("Horde waves: -1 = the data decides (default: sudden), 0 = old start (cutscene + preparation countdown), 1 = always sudden."));
	TAutoConsoleVariable<int32> CVarHordeIntroEnabled(TEXT("Codex.HordeIntro.Enabled"), -1,
		TEXT("Horde start camera flyover: -1 = default (on), 0 = off (the free tactical pause still opens), 1 = on."));
	TAutoConsoleVariable<float> CVarHordeIntroDelay(TEXT("Codex.HordeIntro.FlyoverDelaySeconds"), -1.f,
		TEXT("Horde flyover: real seconds from the spawn to the first shot (<0 = default 1.75)."));
	TAutoConsoleVariable<float> CVarHordeIntroWide(TEXT("Codex.HordeIntro.WideShotSeconds"), -1.f,
		TEXT("Horde flyover: real seconds of the wide shot before the per-type shots (<0 = default 3)."));
	TAutoConsoleVariable<float> CVarHordeIntroRepeatWide(TEXT("Codex.HordeIntro.RepeatWideShotSeconds"), -1.f,
		TEXT("Horde flyover: wide shot of a wave with no new enemy types, real s (<0 = default 2; 0 = no flyover then)."));
	TAutoConsoleVariable<float> CVarHordeIntroType(TEXT("Codex.HordeIntro.TypeShotSeconds"), -1.f,
		TEXT("Horde flyover: real seconds of each per-type shot (<0 = default 2.5)."));
	TAutoConsoleVariable<float> CVarHordeIntroBlend(TEXT("Codex.HordeIntro.ShotBlendSeconds"), -1.f,
		TEXT("Horde flyover: camera glide into each shot, real s (<0 = default 0.8)."));
	TAutoConsoleVariable<float> CVarHordeIntroReturn(TEXT("Codex.HordeIntro.ReturnBlendSeconds"), -1.f,
		TEXT("Horde flyover: camera glide back to the leader, real s (<0 = default 1)."));
	TAutoConsoleVariable<float> CVarHordeIntroTypeDistance(TEXT("Codex.HordeIntro.TypeShotDistance"), -1.f,
		TEXT("Horde flyover: camera distance of a per-type shot, cm (<0 = default 1300)."));
	TAutoConsoleVariable<float> CVarHordeIntroWideMin(TEXT("Codex.HordeIntro.WideMinDistance"), -1.f,
		TEXT("Horde flyover: shortest wide-shot distance, cm (<0 = default 2600)."));
	TAutoConsoleVariable<float> CVarHordeIntroWideMax(TEXT("Codex.HordeIntro.WideMaxDistance"), -1.f,
		TEXT("Horde flyover: longest wide-shot distance, cm (<0 = default 4500)."));
	TAutoConsoleVariable<float> CVarHordeIntroWideBias(TEXT("Codex.HordeIntro.WideFocusBias"), -1.f,
		TEXT("Horde flyover: wide-shot focus between the squad (0) and the horde centre (1) (<0 = default 0.6)."));

	void HordeIntroOverride(float& Value, const TAutoConsoleVariable<float>& CVar)
	{
		const float Override = CVar.GetValueOnGameThread();
		if (Override >= 0.f)
		{
			Value = Override;
		}
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
	HordeIntroOverride(Config.FlyoverDelaySeconds, CVarHordeIntroDelay);
	HordeIntroOverride(Config.WideShotSeconds, CVarHordeIntroWide);
	HordeIntroOverride(Config.RepeatWideShotSeconds, CVarHordeIntroRepeatWide);
	HordeIntroOverride(Config.TypeShotSeconds, CVarHordeIntroType);
	HordeIntroOverride(Config.ShotBlendSeconds, CVarHordeIntroBlend);
	HordeIntroOverride(Config.ReturnBlendSeconds, CVarHordeIntroReturn);
	HordeIntroOverride(Config.TypeShotDistance, CVarHordeIntroTypeDistance);
	HordeIntroOverride(Config.WideMinDistance, CVarHordeIntroWideMin);
	HordeIntroOverride(Config.WideMaxDistance, CVarHordeIntroWideMax);
	HordeIntroOverride(Config.WideFocusBias, CVarHordeIntroWideBias);
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
		return Shots; // a wave of known types: no flyover (the free pause still opens)
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

FHordeIntroCursor HordeIntroRules::Evaluate(const TArray<FHordeIntroShot>& Shots, const FHordeIntroConfig& Config,
	float RealSecondsSinceSpawn)
{
	FHordeIntroCursor Cursor;
	if (Shots.IsEmpty())
	{
		Cursor.bWaiting = false;
		Cursor.bFinished = true;
		return Cursor;
	}
	float Time = RealSecondsSinceSpawn - FMath::Max(0.f, Config.FlyoverDelaySeconds);
	if (Time < 0.f)
	{
		return Cursor; // waiting
	}
	Cursor.bWaiting = false;
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

int32 HordeIntroRules::PickRepresentative(const TArray<FVector>& Candidates, const FVector& SquadCentre)
{
	int32 Best = INDEX_NONE;
	float BestDistance = TNumericLimits<float>::Max();
	for (int32 Index = 0; Index < Candidates.Num(); ++Index)
	{
		const float Distance = FVector::DistSquared2D(Candidates[Index], SquadCentre);
		if (Distance < BestDistance)
		{
			BestDistance = Distance;
			Best = Index;
		}
	}
	return Best;
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
