// Suppressive fire at runtime (game designer decisions 2026-10-26; UE-only, no Godot reference). Pure rules: Combat/SuppressionRules.

#include "Combat/SuppressionSubsystem.h"

#include "Characters/EnemyCharacter.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/EnemyRegistrySubsystem.h"
#include "Combat/HealthComponent.h"
#include "Engine/World.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "HAL/IConsoleManager.h"
#include "Vehicles/VehicleSubsystem.h"

namespace SuppressionRuntime
{
	static TAutoConsoleVariable<int32> CVarSuppression(TEXT("Codex.Combat.Suppression"), 1,
		TEXT("Suppressive fire (game designer 2026-10-26, docs/design/SUPPRESSION.md): 1 = rounds near a unit suppress / pin it (default); 0 = no suppression (the pre-2026-10-26 game), the SUPPRESS order is refused"));

	/** A unit far beyond every radius from both ends of a round is skipped before the segment test, cm. */
	constexpr float SuppressionBroadPhaseSlackCm = 100.f;
}

bool USuppressionSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId USuppressionSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(USuppressionSubsystem, STATGROUP_Tickables);
}

bool USuppressionSubsystem::IsEnabled()
{
	return SuppressionRuntime::CVarSuppression.GetValueOnGameThread() != 0;
}

USuppressionSubsystem* USuppressionSubsystem::Get(const UObject* WorldContext)
{
	const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	return World ? World->GetSubsystem<USuppressionSubsystem>() : nullptr;
}

void USuppressionSubsystem::Report(const UObject* WorldContext, const FSuppressionRound& Round)
{
	if (USuppressionSubsystem* Suppression = Get(WorldContext))
	{
		Suppression->ReportRound(Round);
	}
}

ESuppressionTier USuppressionSubsystem::GetTierOf(const AActor* Unit)
{
	const USuppressionSubsystem* Suppression = Get(Unit);
	return Suppression ? Suppression->GetTier(Unit) : ESuppressionTier::None;
}

float USuppressionSubsystem::GetLevelOf(const AActor* Unit)
{
	const USuppressionSubsystem* Suppression = Get(Unit);
	return Suppression ? Suppression->GetLevel(Unit) : 0.f;
}

bool USuppressionSubsystem::MayEnemyFire(const AActor* Enemy)
{
	return SuppressionRules::EnemyMayFire(GetTierOf(Enemy));
}

FSuppressionFireEffect USuppressionSubsystem::GetOperativeFireOf(const AActor* Operative)
{
	return SuppressionRules::OperativeFire(SuppressionRules::GetConfig(), GetTierOf(Operative));
}

bool USuppressionSubsystem::MayExposeNow(const AActor* Unit)
{
	return SuppressionRules::MayExpose(GetTierOf(Unit));
}

bool USuppressionSubsystem::IsSuppressionOff() const
{
	if (!IsEnabled())
	{
		return true;
	}
	const UGameFlowSubsystem* Flow = GetWorld() ? GetWorld()->GetSubsystem<UGameFlowSubsystem>() : nullptr;
	return Flow && Flow->GetCombatMode() == ECodexCombatMode::TurnBased; // decision 5: no suppression in turn-based combat
}

void USuppressionSubsystem::Tick(float DeltaTime)
{
	if (IsSuppressionOff())
	{
		if (!States.IsEmpty())
		{
			ClearAll(IsEnabled() ? TEXT("turn-based combat") : TEXT("Codex.Combat.Suppression 0"));
		}
		// Turn-based combat (decision 5): the SUPPRESS orders end too (the grid owns every shot).
		if (const USquadSubsystem* Squad = GetWorld() ? GetWorld()->GetSubsystem<USquadSubsystem>() : nullptr)
		{
			for (AOperativeCharacter* Member : Squad->GetMembers())
			{
				if (Member && (Member->HasSuppressOrder() || Member->HasPlannedSuppress()))
				{
					Member->CancelSuppressFire(IsEnabled() ? TEXT("turn-based combat") : TEXT("Codex.Combat.Suppression 0"));
					Member->ClearPlannedSuppress();
				}
			}
		}
		return;
	}
	const FSuppressionConfig& Config = SuppressionRules::GetConfig();
	for (auto It = States.CreateIterator(); It; ++It)
	{
		AActor* Unit = It.Key().Get();
		const UHealthComponent* Health = Unit ? Unit->FindComponentByClass<UHealthComponent>() : nullptr;
		if (!IsValid(Unit) || (Health && !Health->IsAlive()))
		{
			It.RemoveCurrent();
			continue;
		}
		FSuppressionState& State = It.Value();
		const ESuppressionTier Old = State.Tier;
		SuppressionRules::Step(Config, State, DeltaTime);
		NoteTier(Unit, Old, State.Tier);
		if (State.Level <= 0.f && State.Tier == ESuppressionTier::None)
		{
			It.RemoveCurrent();
		}
	}
	for (auto It = Intents.CreateIterator(); It; ++It)
	{
		if (!It.Key().IsValid() || !It.Value().IsValid())
		{
			It.RemoveCurrent();
		}
	}
}

void USuppressionSubsystem::ReportRound(const FSuppressionRound& Round)
{
	UWorld* World = GetWorld();
	if (!World || IsSuppressionOff())
	{
		++Stats.IgnoredRounds;
		return;
	}
	++Stats.Rounds;
	const FSuppressionConfig& Config = SuppressionRules::GetConfig();
	const float Scale = FMath::Max(0.f, Round.WeightScale) * (Round.bDeliberate ? FMath::Max(0.f, Config.DeliberateMultiplier) : 1.f);
	// Broad phase: only units within the largest radius of the round's line count.
	const float Reach = FMath::Max(SuppressionRules::GetNearMissRadius(Config, Round.Weapon), Config.CoverStrikeRadiusCm)
		+ SuppressionRuntime::SuppressionBroadPhaseSlackCm;
	auto Consider = [&](AActor* Unit)
	{
		if (!Unit || Unit == Round.Shooter)
		{
			return;
		}
		const FVector Location = Unit->GetActorLocation();
		if (Unit != Round.Target && FMath::PointDistToSegment(Location, Round.Start, Round.End) > Reach)
		{
			return;
		}
		float Distance = 0.f;
		const ESuppressionImpact Impact = SuppressionRules::ClassifyImpact(Config, Round.Weapon, Round.Start, Round.End, Round.bStruckCover,
			Round.bHitTarget && Unit == Round.Target, Location, Distance);
		const float Pressure = SuppressionRules::ImpactPressure(Config, Round.Weapon, Impact, Distance, Scale);
		if (Pressure > 0.f)
		{
			++Stats.Impacts;
			ApplyPressure(Unit, Pressure);
		}
	};
	if (Round.bAtEnemies)
	{
		for (AEnemyCharacter* Enemy : UEnemyRegistrySubsystem::GetEnemyCharacters(World))
		{
			if (IsValid(Enemy) && !Enemy->IsDying() && Enemy->GetHealthComponent() && Enemy->GetHealthComponent()->IsAlive())
			{
				Consider(Enemy);
			}
		}
		return;
	}
	if (const USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>())
	{
		for (AOperativeCharacter* Member : Squad->GetMembers())
		{
			if (IsValid(Member) && Member->HealthComponent && Member->HealthComponent->IsAlive() && !VehicleQueries::IsMounted(Member))
			{
				Consider(Member);
			}
		}
	}
}

void USuppressionSubsystem::ApplyPressure(AActor* Unit, float Pressure)
{
	FSuppressionState& State = States.FindOrAdd(Unit);
	const ESuppressionTier Old = State.Tier;
	SuppressionRules::AddPressure(SuppressionRules::GetConfig(), State, Pressure);
	NoteTier(Unit, Old, State.Tier);
}

void USuppressionSubsystem::NoteTier(AActor* Unit, ESuppressionTier Old, ESuppressionTier New)
{
	if (Old == New)
	{
		return;
	}
	if (New == ESuppressionTier::Suppressed && Old == ESuppressionTier::None)
	{
		++Stats.SuppressedEntries;
	}
	if (New == ESuppressionTier::Pinned)
	{
		++Stats.PinnedEntries;
	}
	const FSuppressionState* State = States.Find(Unit);
	UE_LOG(LogCodexTactics, Display, TEXT("[Suppression] %s: %s -> %s (level %.2f)"), Unit ? *Unit->GetName() : TEXT("?"),
		SuppressionRules::TierName(Old), SuppressionRules::TierName(New), State ? State->Level : 0.f);
	OnTierChanged.Broadcast(Unit, Old, New);
}

float USuppressionSubsystem::GetLevel(const AActor* Unit) const
{
	if (!Unit || IsSuppressionOff())
	{
		return 0.f;
	}
	const FSuppressionState* State = States.Find(const_cast<AActor*>(Unit));
	return State ? State->Level : 0.f;
}

ESuppressionTier USuppressionSubsystem::GetTier(const AActor* Unit) const
{
	if (!Unit || IsSuppressionOff())
	{
		return ESuppressionTier::None;
	}
	const FSuppressionState* State = States.Find(const_cast<AActor*>(Unit));
	return State ? State->Tier : ESuppressionTier::None;
}

void USuppressionSubsystem::ClearAll(const TCHAR* Why)
{
	TArray<TPair<AActor*, ESuppressionTier>> Freed;
	for (const TPair<TWeakObjectPtr<AActor>, FSuppressionState>& Pair : States)
	{
		if (Pair.Key.IsValid() && Pair.Value.Tier != ESuppressionTier::None)
		{
			Freed.Emplace(Pair.Key.Get(), Pair.Value.Tier);
		}
	}
	States.Reset();
	++Stats.Clears;
	UE_LOG(LogCodexTactics, Display, TEXT("[Suppression] every level cleared (%s)"), Why);
	for (const TPair<AActor*, ESuppressionTier>& Each : Freed)
	{
		OnTierChanged.Broadcast(Each.Key, Each.Value, ESuppressionTier::None);
	}
}

void USuppressionSubsystem::SetLevelForTesting(AActor* Unit, float Level)
{
	if (!Unit)
	{
		return;
	}
	FSuppressionState& State = States.FindOrAdd(Unit);
	const ESuppressionTier Old = State.Tier;
	State.Level = FMath::Clamp(Level, 0.f, 1.f);
	State.QuietTime = 0.f;
	State.Tier = SuppressionRules::ResolveTier(SuppressionRules::GetConfig(), State.Level, State.Tier);
	NoteTier(Unit, Old, State.Tier);
}

void USuppressionSubsystem::SetSuppressIntent(AActor* Shooter, AActor* Target)
{
	if (Shooter && Target)
	{
		Intents.Add(Shooter, Target);
	}
	else if (Shooter)
	{
		Intents.Remove(Shooter);
	}
}

void USuppressionSubsystem::ClearSuppressIntent(const AActor* Shooter)
{
	Intents.Remove(Shooter);
}

AActor* USuppressionSubsystem::GetSuppressIntent(const AActor* Shooter) const
{
	const TWeakObjectPtr<AActor>* Target = Intents.Find(Shooter);
	return Target ? Target->Get() : nullptr;
}

void USuppressionSubsystem::GetTracked(TArray<TPair<AActor*, FSuppressionState>>& Out) const
{
	Out.Reset();
	if (IsSuppressionOff())
	{
		return;
	}
	for (const TPair<TWeakObjectPtr<AActor>, FSuppressionState>& Pair : States)
	{
		if (AActor* Unit = Pair.Key.Get(); Unit && Pair.Value.Level > 0.f)
		{
			Out.Emplace(Unit, Pair.Value);
		}
	}
}
