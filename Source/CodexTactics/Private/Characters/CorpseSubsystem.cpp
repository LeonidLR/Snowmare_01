#include "Characters/CorpseSubsystem.h"

#include "Characters/CorpseBody.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/HealthComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Core/CodexTacticsGameMode.h"
#include "Data/WaveConfigTypes.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"

namespace CorpseTuning
{
	static TAutoConsoleVariable<int32> CVarPersist(TEXT("Codex.Corpse.Persist"), 1,
		TEXT("Enemy corpses (monsters and hostile PMC fighters) stay on the ground (1, game designer 2026-11-08 / 11-09); 0 = the old death decay life span / unmanaged PMC bodies"));
	static TAutoConsoleVariable<int32> CVarCap(TEXT("Codex.Corpse.Cap"), -1,
		TEXT(">= 1 = enemy bodies (monsters + hostile PMC, one cap) kept on the level (-1: the level JSON corpse_cap, default 150)"));
	static TAutoConsoleVariable<float> CVarFadeSeconds(TEXT("Codex.Corpse.FadeSeconds"), 1.f,
		TEXT("Seconds a body over the cap takes to fade out (scaled down)"));
	static TAutoConsoleVariable<int32> CVarFreeze(TEXT("Codex.Corpse.Freeze"), 1,
		TEXT("Kept bodies are frozen once settled (1); 0 = they keep ticking (cost A/B measurements only)"));
	static TAutoConsoleVariable<int32> CVarOldestPool(TEXT("Codex.Corpse.OldestPool"), 10,
		TEXT("The fading body is the farthest from the squad among this many oldest bodies"));

	/** Seconds between two cap checks. */
	constexpr float CapCheckSeconds = 0.5f;
	/** A body rendered within this many seconds is in the camera view. */
	constexpr float InViewSeconds = 0.2f;
}

bool UCorpseSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId UCorpseSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UCorpseSubsystem, STATGROUP_Tickables);
}

FCorpseConfig UCorpseSubsystem::GetConfig() const
{
	FCorpseConfig Config;
	const UWorld* World = GetWorld();
	const ACodexTacticsGameMode* GameMode = World ? World->GetAuthGameMode<ACodexTacticsGameMode>() : nullptr;
	const ULevelConfigAsset* Level = GameMode ? GameMode->GetActiveLevelConfig() : nullptr;
	Config.bPersist = CorpseTuning::CVarPersist.GetValueOnGameThread() != 0;
	Config.Cap = CorpseRules::ResolveCap(Level ? Level->Config.CorpseCap : Config.Cap, CorpseTuning::CVarCap.GetValueOnGameThread());
	Config.FadeSeconds = FMath::Max(0.f, CorpseTuning::CVarFadeSeconds.GetValueOnGameThread());
	Config.OldestPool = FMath::Max(1, CorpseTuning::CVarOldestPool.GetValueOnGameThread());
	return Config;
}

ICorpseBody* UCorpseSubsystem::AsCorpse(AActor* Body)
{
	return IsValid(Body) ? Cast<ICorpseBody>(Body) : nullptr;
}

const ICorpseBody* UCorpseSubsystem::AsCorpse(const AActor* Body)
{
	return AsCorpse(const_cast<AActor*>(Body));
}

bool UCorpseSubsystem::RegisterCorpse(AActor* Body)
{
	UWorld* World = GetWorld();
	if (!World || !AsCorpse(Body) || !GetConfig().bPersist)
	{
		return false;
	}
	if (!IsKept(Body))
	{
		FKeptCorpse& Kept = Corpses.AddDefaulted_GetRef();
		Kept.Body = Body;
		Kept.DeathTime = World->GetTimeSeconds();
	}
	return true;
}

int32 UCorpseSubsystem::GetFrozenCount() const
{
	int32 Count = 0;
	for (const FKeptCorpse& Kept : Corpses)
	{
		Count += Kept.bFrozen && Kept.FadeStart < 0.0 ? 1 : 0;
	}
	return Count;
}

int32 UCorpseSubsystem::GetFadingCount() const
{
	int32 Count = 0;
	for (const FKeptCorpse& Kept : Corpses)
	{
		Count += Kept.FadeStart >= 0.0 ? 1 : 0;
	}
	return Count;
}

TArray<AActor*> UCorpseSubsystem::GetCorpses() const
{
	TArray<AActor*> Out;
	for (const FKeptCorpse& Kept : Corpses)
	{
		if (AActor* Body = Kept.Body.Get())
		{
			Out.Add(Body);
		}
	}
	return Out;
}

bool UCorpseSubsystem::IsKept(const AActor* Body) const
{
	return Body && Corpses.ContainsByPredicate([Body](const FKeptCorpse& Kept) { return Kept.Body.Get() == Body; });
}

bool UCorpseSubsystem::IsFading(const AActor* Body) const
{
	return Body && Corpses.ContainsByPredicate([Body](const FKeptCorpse& Kept) { return Kept.Body.Get() == Body && Kept.FadeStart >= 0.0; });
}

void UCorpseSubsystem::UpdateSettling(double Now, const FCorpseConfig& Config)
{
	for (FKeptCorpse& Kept : Corpses)
	{
		AActor* Actor = Kept.Body.Get();
		ICorpseBody* Body = AsCorpse(Actor);
		if (!Body || Kept.bFrozen || CorpseTuning::CVarFreeze.GetValueOnGameThread() == 0)
		{
			continue;
		}
		const bool bSettled = Body->IsDeathPoseSettled();
		if (!bSettled)
		{
			Kept.SettledTime = -1.0;
		}
		else if (Kept.SettledTime < 0.0)
		{
			Kept.SettledTime = Now;
		}
		const float SinceDeath = static_cast<float>(Now - Kept.DeathTime);
		const float SinceSettled = Kept.SettledTime < 0.0 ? -1.f : static_cast<float>(Now - Kept.SettledTime);
		if (CorpseRules::ShouldFreeze(SinceDeath, SinceSettled, Body->IsCorpseFalling(), Body->IsCorpseOnLadder(), Config))
		{
			Body->FreezeCorpse();
			Kept.bFrozen = true;
			UE_LOG(LogCodexTactics, Verbose, TEXT("[Corpse] %s frozen %.2f s after its death (%s)"), *Actor->GetName(), SinceDeath,
				SinceSettled >= 0.f ? TEXT("death pose settled") : TEXT("settle time out"));
		}
	}
}

void UCorpseSubsystem::ApplyCap(const FCorpseConfig& Config, double Now)
{
	TArray<FCorpseCandidate> Candidates;
	Candidates.Reserve(Corpses.Num());
	TArray<FVector, TInlineAllocator<8>> Squaddies;
	if (const USquadSubsystem* Squad = GetWorld()->GetSubsystem<USquadSubsystem>())
	{
		for (const AOperativeCharacter* Member : Squad->GetMembers())
		{
			if (Member && Member->HealthComponent && Member->HealthComponent->IsAlive())
			{
				Squaddies.Add(Member->GetActorLocation());
			}
		}
	}
	for (const FKeptCorpse& Kept : Corpses)
	{
		FCorpseCandidate& Candidate = Candidates.AddDefaulted_GetRef();
		const AActor* Body = Kept.Body.Get();
		const ICorpseBody* Corpse = AsCorpse(Body);
		const UPrimitiveComponent* Visual = Corpse ? Corpse->GetCorpseVisual() : nullptr;
		Candidate.DeathTime = Kept.DeathTime;
		Candidate.bFrozen = Kept.bFrozen;
		Candidate.bFading = Kept.FadeStart >= 0.0;
		Candidate.bInView = Visual && Visual->WasRecentlyRendered(CorpseTuning::InViewSeconds);
		Candidate.bUnsearchedLoot = Corpse && Corpse->HoldsUnsearchedLoot();
		Candidate.SquadDistanceCm = Squaddies.IsEmpty() ? 0.f : TNumericLimits<float>::Max();
		for (const FVector& Point : Squaddies)
		{
			if (Body)
			{
				Candidate.SquadDistanceCm = FMath::Min(Candidate.SquadDistanceCm, static_cast<float>(FVector::Dist(Point, Body->GetActorLocation())));
			}
		}
	}
	for (int32 Over = CorpseRules::CountOverCap(Candidates, Config.Cap); Over > 0; --Over)
	{
		const int32 Pick = CorpseRules::PickCorpseToFade(Candidates, Config);
		if (Pick == INDEX_NONE)
		{
			break; // only bodies still settling: wait for them
		}
		Candidates[Pick].bFading = true;
		FKeptCorpse& Kept = Corpses[Pick];
		Kept.FadeStart = Now;
		if (const ICorpseBody* Corpse = AsCorpse(Kept.Body.Get()); Corpse && Corpse->GetCorpseVisual())
		{
			Kept.BaseScale = Corpse->GetCorpseVisual()->GetComponentScale();
		}
		UE_LOG(LogCodexTactics, Display, TEXT("[Corpse] over the cap of %d: %s fades out (%.0f m from the squad%s)"), Config.Cap,
			*GetNameSafe(Kept.Body.Get()), Candidates[Pick].SquadDistanceCm / 100.f, Candidates[Pick].bInView ? TEXT(", in view: no hidden body left") : TEXT(""));
	}
}

void UCorpseSubsystem::UpdateFades(double Now, const FCorpseConfig& Config)
{
	for (int32 Index = Corpses.Num() - 1; Index >= 0; --Index)
	{
		FKeptCorpse& Kept = Corpses[Index];
		AActor* Body = Kept.Body.Get();
		if (!Body || Body->IsActorBeingDestroyed())
		{
			Corpses.RemoveAt(Index); // gone another way (level cleanup, a load, the arena's next round)
			continue;
		}
		if (Kept.FadeStart < 0.0)
		{
			continue;
		}
		const float Scale = CorpseRules::FadeScale(static_cast<float>(Now - Kept.FadeStart), Config.FadeSeconds);
		if (Scale <= 0.f)
		{
			Body->Destroy();
			Corpses.RemoveAt(Index);
			++FadedTotal;
			continue;
		}
		if (UPrimitiveComponent* Visual = AsCorpse(Body) ? AsCorpse(Body)->GetCorpseVisual() : nullptr)
		{
			Visual->SetWorldScale3D(Kept.BaseScale * Scale); // shrinks into its root on the ground
		}
	}
}

void UCorpseSubsystem::Tick(float DeltaTime)
{
	UWorld* World = GetWorld();
	if (!World || Corpses.IsEmpty())
	{
		return;
	}
	Corpses.RemoveAll([](const FKeptCorpse& Kept) { return !Kept.Body.IsValid() || Kept.Body->IsActorBeingDestroyed(); });
	const FCorpseConfig Config = GetConfig();
	const double Now = World->GetTimeSeconds();
	UpdateSettling(Now, Config);
	CapTimer -= DeltaTime;
	if (CapTimer <= 0.f || Corpses.Num() > Config.Cap + 8)
	{
		CapTimer = CorpseTuning::CapCheckSeconds;
		ApplyCap(Config, Now);
	}
	UpdateFades(Now, Config);
}

namespace CorpseCommands
{
	static FAutoConsoleCommandWithWorld Status(
		TEXT("CodexTactics.Corpse.Status"),
		TEXT("Logs the enemy bodies (monsters + hostile PMC) kept on this level (frozen / settling / fading) and the cap."),
		FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World)
		{
			if (const UCorpseSubsystem* Corpses = World ? World->GetSubsystem<UCorpseSubsystem>() : nullptr)
			{
				const FCorpseConfig Config = Corpses->GetConfig();
				UE_LOG(LogCodexTactics, Display, TEXT("[Corpse] %d bodies kept (%d frozen, %d fading), cap %d, %d faded out so far, persist %s"),
					Corpses->GetCorpseCount(), Corpses->GetFrozenCount(), Corpses->GetFadingCount(), Config.Cap, Corpses->GetFadedTotal(),
					Config.bPersist ? TEXT("on") : TEXT("off"));
			}
		}));
}
