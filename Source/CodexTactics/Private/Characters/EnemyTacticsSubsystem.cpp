#include "Characters/EnemyTacticsSubsystem.h"

#include "Characters/EnemyCharacter.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/EnemyRegistrySubsystem.h"
#include "Combat/HealthComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Combat/TacticalSightSubsystem.h"
#include "HAL/IConsoleManager.h"

namespace EnemyTacticsTuning
{
	// Intelligence knobs of the Jev AI coach (Scripts/Tools/jev_ai_coach.py, -dpcvars= / ai_tuning.json).
	static TAutoConsoleVariable<int32> CVarTactics(TEXT("Codex.Enemy.Tactics"), 1, TEXT("Melee pack tactics on (1) / beeline (0)"));
	static TAutoConsoleVariable<float> CVarFlankScale(TEXT("Codex.Enemy.FlankShareScale"), 1.f, TEXT("Multiplier of every archetype's flank share"));
	static TAutoConsoleVariable<int32> CVarCapBonus(TEXT("Codex.Enemy.FocusCapBonus"), 0, TEXT("Added to every archetype's attackers-per-target cap"));
	static TAutoConsoleVariable<float> CVarPreference(TEXT("Codex.Enemy.PreferenceScale"), 1.f,
		TEXT("Multiplier of the target preferences (wounded / isolated / exposed / back)"));
	static TAutoConsoleVariable<int32> CVarMorale(TEXT("Codex.Enemy.Morale"), 1, TEXT("Fragile enemies fall back when hurt (1) / never (0)"));
	static TAutoConsoleVariable<int32> CVarMoraleDeaths(TEXT("Codex.Enemy.MoraleDeathsBonus"), 0,
		TEXT("Added to the pack-mate deaths nearby that break a fragile enemy's morale"));

	// Flank detour (user decision 2026-10-18): a real detour to the flank point before closing in.
	static TAutoConsoleVariable<int32> CVarFlankDetour(TEXT("Codex.Enemy.FlankDetour"), 1,
		TEXT("Flankers walk to their flank point first (1) / the old cut straight in within 5 m of the target (0, A/B)"));
	static TAutoConsoleVariable<float> CVarFlankArrive(TEXT("Codex.Enemy.FlankArriveCm"), 150.f, TEXT("A flanker is at its flank point this close, cm"));
	static TAutoConsoleVariable<float> CVarFlankCone(TEXT("Codex.Enemy.FlankSideConeDeg"), 60.f,
		TEXT("A flanker within bite reach this close (deg) to the flank side of its target closes in at once"));
	static TAutoConsoleVariable<float> CVarFlankTimeout(TEXT("Codex.Enemy.FlankTimeout"), 8.f, TEXT("A flank detour is given up after this long, s"));
	static TAutoConsoleVariable<float> CVarFlankStuck(TEXT("Codex.Enemy.FlankStuckSeconds"), 1.5f,
		TEXT("A flank detour without 50 cm of progress this long (a blocked point) goes straight in, s"));

	constexpr double RefreshSeconds = 0.4;
	constexpr double FallBackSeconds = 3.5;
	constexpr double MoraleCooldownSeconds = 20.0;
	constexpr double DeathMemorySeconds = 6.0;
	constexpr float DeathRadius = 1000.f;
	constexpr float PackRadius = 1500.f;

	FEnemyTacticsProfile Tuned(EEnemyArchetype Archetype)
	{
		FEnemyTacticsProfile Profile = EnemyTacticsRules::ProfileFor(Archetype);
		const float Preference = CVarPreference.GetValueOnGameThread();
		Profile.WoundedWeight *= Preference;
		Profile.IsolatedWeight *= Preference;
		Profile.ExposedWeight *= Preference;
		Profile.BackWeight *= Preference;
		Profile.FlankShare = FMath::Clamp(Profile.FlankShare * CVarFlankScale.GetValueOnGameThread(), 0.f, 1.f);
		Profile.MaxAttackersPerTarget = FMath::Max(1, Profile.MaxAttackersPerTarget + CVarCapBonus.GetValueOnGameThread());
		if (Profile.MoraleNearbyDeaths > 0)
		{
			Profile.MoraleNearbyDeaths = FMath::Max(1, Profile.MoraleNearbyDeaths + CVarMoraleDeaths.GetValueOnGameThread());
		}
		if (CVarMorale.GetValueOnGameThread() == 0)
		{
			Profile.MoraleHealthFraction = 0.f;
			Profile.MoraleNearbyDeaths = 0;
		}
		return Profile;
	}
}

FFlankDetourConfig UEnemyTacticsSubsystem::GetFlankDetourConfig()
{
	FFlankDetourConfig Config;
	Config.bLegacyFiveMetreCut = EnemyTacticsTuning::CVarFlankDetour.GetValueOnGameThread() == 0;
	Config.ArriveCm = FMath::Max(EnemyTacticsTuning::CVarFlankArrive.GetValueOnGameThread(), 10.f);
	Config.SideConeDeg = FMath::Clamp(EnemyTacticsTuning::CVarFlankCone.GetValueOnGameThread(), 0.f, 180.f);
	Config.TimeoutSeconds = FMath::Max(EnemyTacticsTuning::CVarFlankTimeout.GetValueOnGameThread(), 0.f);
	Config.StuckSeconds = FMath::Max(EnemyTacticsTuning::CVarFlankStuck.GetValueOnGameThread(), 0.1f);
	return Config;
}

bool UEnemyTacticsSubsystem::IsCoordinated(const AEnemyCharacter* Enemy)
{
	if (!Enemy || Enemy->IsDying())
	{
		return false;
	}
	const EEnemyArchetype Archetype = Enemy->GetArchetype();
	return Archetype == EEnemyArchetype::FrostHound || Archetype == EEnemyArchetype::Cutter || Archetype == EEnemyArchetype::Frostbitten
		|| Archetype == EEnemyArchetype::Brute || Archetype == EEnemyArchetype::Base;
}

bool UEnemyTacticsSubsystem::GetOrder(AEnemyCharacter* Enemy, FEnemyTacticOrder& OutOrder)
{
	if (EnemyTacticsTuning::CVarTactics.GetValueOnGameThread() == 0 || !IsCoordinated(Enemy))
	{
		return false;
	}
	if (GetWorld()->GetTimeSeconds() - LastRefresh >= EnemyTacticsTuning::RefreshSeconds)
	{
		Refresh();
	}
	const FEnemyTacticOrder* Order = Orders.Find(Enemy);
	if (!Order)
	{
		return false;
	}
	OutOrder = *Order;
	return true;
}

void UEnemyTacticsSubsystem::NotifyEnemyDied(const FVector& Location)
{
	RecentDeaths.Add(TPair<FVector, double>(Location, GetWorld()->GetTimeSeconds()));
}

void UEnemyTacticsSubsystem::Refresh()
{
	using namespace EnemyTacticsTuning;
	UWorld* World = GetWorld();
	const double Now = World->GetTimeSeconds();
	LastRefresh = Now;
	RecentDeaths.RemoveAll([Now](const TPair<FVector, double>& Death) { return Now - Death.Value > DeathMemorySeconds; });

	// The operatives as the tactics see them.
	TArray<AOperativeCharacter*> Operatives;
	TArray<FEnemyTacticsTarget> Targets;
	FVector Centre = FVector::ZeroVector;
	if (const USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>())
	{
		for (AOperativeCharacter* Member : Squad->GetMembers())
		{
			if (Member && Member->HealthComponent && Member->HealthComponent->IsAlive())
			{
				Operatives.Add(Member);
				FEnemyTacticsTarget& Target = Targets.AddDefaulted_GetRef();
				Target.Location = Member->GetActorLocation();
				Target.Forward = Member->GetActorForwardVector();
				Target.HealthFraction = Member->HealthComponent->GetCurrentHealth() / FMath::Max(Member->HealthComponent->GetMaxHealth(), 1.f);
				Target.bInCover = Member->IsInBarricadeCover();
				Centre += Target.Location;
			}
		}
	}
	TMap<TWeakObjectPtr<AEnemyCharacter>, FEnemyTacticOrder> Previous = MoveTemp(Orders);
	Orders.Reset();
	if (Operatives.IsEmpty())
	{
		return;
	}
	Centre /= Operatives.Num();
	for (int32 Index = 0; Index < Targets.Num(); ++Index)
	{
		Targets[Index].NearestMateDistance = 100000.f;
		for (int32 Other = 0; Other < Targets.Num(); ++Other)
		{
			if (Other != Index)
			{
				Targets[Index].NearestMateDistance = FMath::Min(Targets[Index].NearestMateDistance,
					static_cast<float>(FVector::Dist2D(Targets[Index].Location, Targets[Other].Location)));
			}
		}
	}

	// The commanded enemies, the closest to the squad first (they claim their targets first).
	TArray<AEnemyCharacter*> Enemies;
	for (AEnemyCharacter* Enemy : UEnemyRegistrySubsystem::GetEnemyCharacters(World))
	{
		if (IsCoordinated(Enemy))
		{
			Enemies.Add(Enemy);
		}
	}
	Enemies.Sort([&Centre](const AEnemyCharacter& A, const AEnemyCharacter& B)
	{
		return FVector::DistSquared2D(A.GetActorLocation(), Centre) < FVector::DistSquared2D(B.GetActorLocation(), Centre);
	});

	TArray<TArray<AEnemyCharacter*>> AttackersOf;
	AttackersOf.SetNum(Operatives.Num());
	for (AEnemyCharacter* Enemy : Enemies)
	{
		const FEnemyTacticsProfile Profile = Tuned(Enemy->GetArchetype());
		const FVector Location = Enemy->GetActorLocation();

		// Morale: a running fall-back holds; else a fresh check (once per cooldown).
		const double* Until = FallBackUntil.Find(Enemy);
		bool bFallingBack = Until && *Until > Now;
		if (!bFallingBack && (!MoraleCooldownUntil.Contains(Enemy) || MoraleCooldownUntil[Enemy] <= Now))
		{
			int32 Deaths = 0;
			for (const TPair<FVector, double>& Death : RecentDeaths)
			{
				Deaths += FVector::Dist2D(Death.Key, Location) <= DeathRadius ? 1 : 0;
			}
			const UHealthComponent* Health = Enemy->GetHealthComponent();
			const float HealthFraction = Health ? Health->GetCurrentHealth() / FMath::Max(Health->GetMaxHealth(), 1.f) : 1.f;
			if (EnemyTacticsRules::ShouldFallBack(Profile, HealthFraction, Deaths))
			{
				bFallingBack = true;
				FallBackUntil.Add(Enemy, Now + FallBackSeconds);
				MoraleCooldownUntil.Add(Enemy, Now + MoraleCooldownSeconds);
				FVector Pack = FVector::ZeroVector;
				int32 PackCount = 0;
				for (const AEnemyCharacter* Mate : Enemies)
				{
					if (Mate != Enemy && Mate->GetArchetype() == Enemy->GetArchetype() && FVector::Dist2D(Mate->GetActorLocation(), Location) <= PackRadius)
					{
						Pack += Mate->GetActorLocation();
						++PackCount;
					}
				}
				Pack = PackCount > 0 ? Pack / PackCount : Pack;
				FEnemyTacticOrder& Order = Orders.Add(Enemy);
				Order.Role = EEnemyTacticRole::FallBack;
				Order.MovePoint = EnemyTacticsRules::FallBackPoint(Location, Centre, PackCount > 0 ? &Pack : nullptr);
				++FallBacks;
				++SummaryFallBacks;
				continue;
			}
		}
		if (bFallingBack)
		{
			if (const FEnemyTacticOrder* Old = Previous.Find(Enemy))
			{
				Orders.Add(Enemy, *Old);
			}
			continue;
		}

		// A target: the profile's cheapest one this enemy can reach.
		TArray<FEnemyTacticsTarget> Mine = Targets;
		for (int32 Index = 0; Index < Mine.Num(); ++Index)
		{
			Mine[Index].bUsable = Enemy->IsTargetUsableForTactics(Operatives[Index]);
			// Sprint 08: where this enemy believes him to be (last known spot when unperceived).
			FVector Belief;
			if (const UTacticalSightSubsystem* Sight = GetWorld()->GetSubsystem<UTacticalSightSubsystem>();
				Sight && Sight->GetBelief(Enemy, Operatives[Index], Belief))
			{
				Mine[Index].Location = Belief;
			}
			const FEnemyTacticOrder* Old = Previous.Find(Enemy);
			Mine[Index].bCurrent = Old && Old->Target.Get() == Operatives[Index];
		}
		const int32 Chosen = EnemyTacticsRules::ChooseTarget(Profile, Location, Mine);
		if (Chosen == INDEX_NONE)
		{
			continue;
		}
		if (const FEnemyTacticOrder* Old = Previous.Find(Enemy); Old && Old->Target.IsValid() && Old->Target.Get() != Operatives[Chosen])
		{
			++SummaryRetargets;
		}
		++Targets[Chosen].Attackers;
		AttackersOf[Chosen].Add(Enemy);
		FEnemyTacticOrder& Order = Orders.Add(Enemy);
		Order.Target = Operatives[Chosen];
	}

	// Roles per target: a share goes round his flank, the rest pin him; returning attackers keep their role.
	for (int32 Index = 0; Index < Operatives.Num(); ++Index)
	{
		TArray<AEnemyCharacter*>& Attackers = AttackersOf[Index];
		const FVector TargetLocation = Targets[Index].Location;
		Attackers.Sort([&TargetLocation](const AEnemyCharacter& A, const AEnemyCharacter& B)
		{
			return FVector::DistSquared2D(A.GetActorLocation(), TargetLocation) < FVector::DistSquared2D(B.GetActorLocation(), TargetLocation);
		});
		TArray<FEnemyTacticsProfile> Profiles;
		TArray<const EEnemyTacticRole*> PreviousRoles;
		for (AEnemyCharacter* Enemy : Attackers)
		{
			Profiles.Add(Tuned(Enemy->GetArchetype()));
			const FEnemyTacticOrder* Old = Previous.Find(Enemy);
			PreviousRoles.Add(Old && Old->Target == Operatives[Index] && Old->Role != EEnemyTacticRole::FallBack ? &Old->Role : nullptr);
		}
		const TArray<EEnemyTacticRole> Roles = EnemyTacticsRules::AssignRoles(Profiles, PreviousRoles);
		for (int32 Rank = 0; Rank < Attackers.Num(); ++Rank)
		{
			AEnemyCharacter* Enemy = Attackers[Rank];
			FEnemyTacticOrder& Order = Orders.FindChecked(Enemy);
			Order.Role = Roles[Rank];
			const bool bWasFlanking = PreviousRoles[Rank] && *PreviousRoles[Rank] == EEnemyTacticRole::Flank;
			if (Order.Role == EEnemyTacticRole::Flank)
			{
				Order.MovePoint = bWasFlanking ? Previous.FindChecked(Enemy).MovePoint
					: EnemyTacticsRules::FlankPoint(Enemy->GetActorLocation(), TargetLocation, Centre);
				if (!bWasFlanking)
				{
					++FlankOrders;
					++SummaryFlanks;
				}
			}
		}
	}

	if (Now - LastSummary >= 10.0 && (SummaryFlanks || SummaryFallBacks || SummaryBackstabs))
	{
		LastSummary = Now;
		FString PerTarget;
		for (int32 Index = 0; Index < Operatives.Num(); ++Index)
		{
			PerTarget += FString::Printf(TEXT("%s%s %d"), PerTarget.IsEmpty() ? TEXT("") : TEXT(", "), *Operatives[Index]->DisplayName.ToString(),
				AttackersOf[Index].Num());
		}
		UE_LOG(LogCodexTactics, Display, TEXT("[EnemyTactics] flank orders %d, fallbacks %d, backstabs %d, retargets %d (10 s); melee %d on: %s"),
			SummaryFlanks, SummaryFallBacks, SummaryBackstabs, SummaryRetargets, Enemies.Num(), *PerTarget);
		SummaryFlanks = 0;
		SummaryFallBacks = 0;
		SummaryBackstabs = 0;
		SummaryRetargets = 0;
	}
}
