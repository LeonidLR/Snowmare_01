#include "Characters/PanicComponent.h"

#include "Characters/OperativeCharacter.h"
#include "Characters/RageComponent.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/HealthComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Interactables/HeatSourceComponent.h"
#include "Interactables/RadiusRingSubsystem.h"
#include "Subsystems/CodexEventBus.h"
#include "Survival/ColdSurvivalComponent.h"
#include "UI/FloatingTextSubsystem.h"
#include "UI/GameMessageSubsystem.h"

namespace
{
	const FLinearColor PanicRed(1.f, 0.2f, 0.2f);

	void PanicLinePost(const AOperativeCharacter* Operative, const FString& Text)
	{
		UWorld* World = Operative ? Operative->GetWorld() : nullptr;
		if (UGameMessageSubsystem* Messages = World ? World->GetSubsystem<UGameMessageSubsystem>() : nullptr)
		{
			Messages->PostMessage(Operative->DisplayName, FText::FromString(Text));
		}
	}

	bool IsLivePanicThreat(const AActor* Actor)
	{
		if (!IsValid(Actor) || !Actor->ActorHasTag(FName(TEXT("Enemy"))))
		{
			return false;
		}
		const UHealthComponent* Health = Actor->FindComponentByClass<UHealthComponent>();
		return !Health || Health->IsAlive();
	}

	float PanicFortitudeOf(const AOperativeCharacter* Operative)
	{
		return Operative && Operative->ColdSurvival ? Operative->ColdSurvival->Fortitude : 5.f; // Godot default 5
	}
}

UPanicComponent::UPanicComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

AOperativeCharacter* UPanicComponent::GetOperative() const
{
	return Cast<AOperativeCharacter>(GetOwner());
}

bool UPanicComponent::IsCombatActive() const
{
	const UGameFlowSubsystem* Flow = GetWorld() ? GetWorld()->GetSubsystem<UGameFlowSubsystem>() : nullptr;
	return Flow && Flow->GetPhase() == ECodexGamePhase::WaveCombat && Flow->GetCombatMode() == ECodexCombatMode::RealTime;
}

void UPanicComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	const AOperativeCharacter* Operative = GetOperative();
	if (!bEnabled || !Operative || !Operative->bRecruited || !Operative->HealthComponent || !Operative->HealthComponent->IsAlive())
	{
		Stress = 0.f;
		if (bPanicking)
		{
			RecoverFromPanic(TEXT("Механика паники отключена"), true);
		}
		return;
	}
	// Godot: the panic belongs to the real-time fight; the turn-based grid fight does not use it.
	if (bPanicking && !IsCombatActive())
	{
		RecoverFromPanic(TEXT("Бой окончен"), true);
	}
	ProcessStress(DeltaTime);
	if (bPanicking)
	{
		ProcessPanicState(DeltaTime);
	}
}

void UPanicComponent::ProcessStress(float DeltaSeconds)
{
	AOperativeCharacter* Operative = GetOperative();
	const bool bWarm = Operative->ColdSurvival && Operative->ColdSurvival->IsNearHeatSource();
	if (bWarm)
	{
		WarmthTimer += DeltaSeconds;
		Stress = FMath::Max(0.f, Stress - Config.HeatSourceCalmRate * DeltaSeconds);
		if (bPanicking && WarmthTimer >= Config.HeatSourcePanicBreakTime)
		{
			RecoverFromPanic(TEXT("Согрелся у источника тепла"));
			return;
		}
	}
	else
	{
		WarmthTimer = 0.f;
	}
	if (!IsCombatActive())
	{
		// Godot: out of the fight the operatives only freeze; the stress calms down.
		Stress = FMath::Max(0.f, Stress - Config.StressRecoveryRate * Config.StressRecoveryMultiplier * DeltaSeconds);
		bStressShown = false;
		return;
	}
	if (bPanicking)
	{
		return;
	}
	const UHealthComponent* Health = Operative->HealthComponent;
	const int32 Ammo = Operative->UsesAmmo() ? Operative->ReserveAmmo : 999;
	const float Growth = PanicRules::GetStressGrowth(Config, Health->GetCurrentHealth() / FMath::Max(1.f, Health->GetMaxHealth()),
		Operative->ColdLevel / 100.f, Ammo, GetNearestEnemyDistance(), PanicFortitudeOf(Operative));
	Stress = PanicRules::StepStress(Config, Stress, Growth, DeltaSeconds);
	bStressShown = Stress >= 50.f;
	if (Stress >= 100.f)
	{
		AttemptTriggerPanic();
	}
}

void UPanicComponent::OnDamageTaken(float FinalDamage)
{
	const AOperativeCharacter* Operative = GetOperative();
	if (!bEnabled || bPanicking || !IsCombatActive() || !Operative)
	{
		return;
	}
	Stress = FMath::Min(100.f, Stress + PanicRules::GetDamageStress(Config, FinalDamage, PanicFortitudeOf(Operative)));
	bStressShown = Stress >= 50.f;
	if (Stress >= 100.f)
	{
		AttemptTriggerPanic();
	}
}

void UPanicComponent::OnLowAmmo()
{
	const AOperativeCharacter* Operative = GetOperative();
	if (!bEnabled || bPanicking || !IsCombatActive() || !Operative)
	{
		return;
	}
	Stress = FMath::Min(100.f, Stress + PanicRules::GetLowAmmoStress(Config, PanicFortitudeOf(Operative)));
	if (Stress >= 100.f)
	{
		AttemptTriggerPanic();
	}
}

void UPanicComponent::AttemptTriggerPanic()
{
	AOperativeCharacter* Operative = GetOperative();
	if (!bEnabled || !IsCombatActive() || !Operative)
	{
		return;
	}
	const USquadSubsystem* Squad = GetWorld()->GetSubsystem<USquadSubsystem>();
	switch (PanicRules::GetTriggerVerdict(Config, CountPanickedSquadMembers(), Squad && Squad->GetLeader() == Operative))
	{
	case PanicRules::ETriggerVerdict::HoldOnLimit:
		Stress = 95.f; // the limit is reached: he holds on with his last strength
		return;
	case PanicRules::ETriggerVerdict::HoldOnLeader:
		Stress = 90.f; // subordinates panic: the leader holds on to the end
		return;
	default:
		break;
	}
	FString Reason = TEXT("Критический стресс: ");
	const UHealthComponent* Health = Operative->HealthComponent;
	if (Health && Health->GetCurrentHealth() / FMath::Max(1.f, Health->GetMaxHealth()) < Config.HpThreshold)
	{
		Reason += TEXT("[Тяжелое ранение] ");
	}
	if (Operative->ColdLevel / 100.f > Config.ColdThreshold)
	{
		Reason += TEXT("[Обморожение] ");
	}
	if (Operative->UsesAmmo() && Operative->ReserveAmmo <= Config.LowAmmoThreshold)
	{
		Reason += TEXT("[Нет патронов] ");
	}
	TriggerPanic(Reason);
}

void UPanicComponent::TriggerPanic(const FString& Reason, bool bForce)
{
	AOperativeCharacter* Operative = GetOperative();
	if (bPanicking || !Operative || (!bForce && !IsCombatActive()))
	{
		return;
	}
	if (Operative->IsRaging())
	{
		return; // Godot: a raging soldier does not panic
	}
	bPanicking = true;
	Phase = EPanicPhase::Fleeing;
	FleePhaseTimer = 0.f;
	PanicTimer = FMath::FRandRange(Config.MinDuration, Config.MaxDuration);
	Stress = 100.f;
	WarmthTimer = 0.f;
	PanicOrigin = Operative->GetActorLocation();
	CachedFleeDirection = FVector::ZeroVector;
	// Drops the player's orders and the reload; stands up to run (Godot set_stance(STANDING, true)).
	Operative->StopOperative();
	Operative->SetStance(EOperativeStance::Standing);
	UFloatingTextSubsystem::SpawnAboveOperative(Operative, TEXT("😱 ПАНИКА!"), PanicRed);
	static const TCHAR* Phrases[] = {
		TEXT("😱 Я больше не могу! Они повсюду!"),
		TEXT("😱 Отступаем, отходим! Нас сомнут!"),
		TEXT("😱 Оружие заклинило! Я ухожу из сектора!"),
		TEXT("😱 Назад, назад к теплу! Спасайтесь!") };
	PanicLinePost(Operative, Phrases[FMath::RandRange(0, UE_ARRAY_COUNT(Phrases) - 1)]);
	UE_LOG(LogCodexTactics, Display, TEXT("%s panics (%s)"), *Operative->DisplayName.ToString(), *Reason);
	UpdateAura(true);
	OnPanicChanged.Broadcast(true);
	if (UCodexEventBus* Bus = UCodexEventBus::Get(this))
	{
		Bus->OnSoldierPanicked.Broadcast(Operative, Reason);
	}
}

void UPanicComponent::TransitionToCowering()
{
	AOperativeCharacter* Operative = GetOperative();
	if (!bPanicking || Phase == EPanicPhase::Cowering || !Operative)
	{
		return;
	}
	Phase = EPanicPhase::Cowering;
	Operative->StopOperative();
	Operative->SetStance(EOperativeStance::Crouching);
	UFloatingTextSubsystem::SpawnAboveOperative(Operative, TEXT("🧎 СЖАЛСЯ В СТРАХЕ"), FLinearColor(1.f, 0.4f, 0.4f));
}

void UPanicComponent::RecoverFromPanic(const FString& Reason, bool bSilent)
{
	if (!bPanicking)
	{
		return;
	}
	AOperativeCharacter* Operative = GetOperative();
	bPanicking = false;
	Phase = EPanicPhase::None;
	Stress = 40.f;
	PanicTimer = 0.f;
	WarmthTimer = 0.f;
	UpdateAura(false);
	if (Operative)
	{
		Operative->StopOperative();
		if (Operative->HealthComponent && Operative->HealthComponent->IsAlive())
		{
			Operative->SetStance(EOperativeStance::Standing); // back on his feet, ready to fight
		}
		Operative->ApplyMovementParams(); // the flight speed is over
		if (!bSilent)
		{
			static const TCHAR* Phrases[] = {
				TEXT("😮‍💨 Взял себя в руки! Возвращаюсь в строй!"),
				TEXT("😮‍💨 Согрелся, дыхание ровное, готов к бою!"),
				TEXT("😮‍💨 Паника отступила. Сектор чист, держу позицию!") };
			PanicLinePost(Operative, Phrases[FMath::RandRange(0, UE_ARRAY_COUNT(Phrases) - 1)]);
			UFloatingTextSubsystem::SpawnAboveOperative(Operative, TEXT("😮‍💨 ПРИШЕЛ В СЕБЯ"), FLinearColor(0.3f, 1.f, 0.5f));
		}
		UE_LOG(LogCodexTactics, Display, TEXT("%s recovers from panic (%s)"), *Operative->DisplayName.ToString(), *Reason);
	}
	OnPanicChanged.Broadcast(false);
	if (UCodexEventBus* Bus = UCodexEventBus::Get(this))
	{
		Bus->OnSoldierCalmed.Broadcast(Operative, Reason);
	}
}

void UPanicComponent::ProcessPanicState(float DeltaSeconds)
{
	AOperativeCharacter* Operative = GetOperative();
	if (!Operative)
	{
		return;
	}
	PanicTimer -= DeltaSeconds;
	UpdateAura(true);
	UCharacterMovementComponent* Movement = Operative->GetCharacterMovement();
	if (Phase == EPanicPhase::Fleeing)
	{
		// Godot _process_panic_movement: run upright at the flee speed in the flee direction.
		FleePhaseTimer += DeltaSeconds;
		const FVector Direction = GetFleeDirection();
		if (Movement)
		{
			Movement->MaxWalkSpeed = Config.FleeSpeed > 0.f ? Config.FleeSpeed * 100.f : Movement->MaxWalkSpeed * Config.SpeedMultiplier;
		}
		Operative->AddMovementInput(Direction);
		if (FVector::Dist2D(Operative->GetActorLocation(), PanicOrigin) >= Config.FleeDistance * 100.f || FleePhaseTimer >= Config.FleeMaxTime)
		{
			TransitionToCowering();
		}
	}
	// A calm leader within 8 m shortens the panic (Godot: timer -= delta * 0.5).
	const USquadSubsystem* Squad = GetWorld()->GetSubsystem<USquadSubsystem>();
	const AOperativeCharacter* Leader = Squad ? Squad->GetLeader() : nullptr;
	if (Leader && Leader != Operative && FVector::Dist(Leader->GetActorLocation(), Operative->GetActorLocation()) < 800.f
		&& !(Leader->PanicComponent && Leader->PanicComponent->IsPanicking()))
	{
		PanicTimer -= DeltaSeconds * 0.5f;
	}
	const float Nearest = GetNearestEnemyDistance();
	if (PanicTimer <= 0.f && (Nearest > Config.RecoverDistanceFromEnemies || Nearest < 0.f))
	{
		RecoverFromPanic(TEXT("Оторвался от врагов"));
	}
	else if (PanicTimer <= -2.f)
	{
		RecoverFromPanic(TEXT("Время паники истекло"));
	}
}

FVector UPanicComponent::GetFleeDirection()
{
	const AOperativeCharacter* Operative = GetOperative();
	UWorld* World = GetWorld();
	if (!Operative || !World)
	{
		return FVector::ZeroVector;
	}
	const FVector Here = Operative->GetActorLocation();
	FVector Flee = FVector::ZeroVector;
	// 1. Away from the enemies within 20 m (closer ones push harder).
	FVector Repulsion = FVector::ZeroVector;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if (IsLivePanicThreat(*It))
		{
			const float Distance = FVector::Dist2D(Here, It->GetActorLocation()) / 100.f;
			if (Distance < 20.f && Distance > 0.01f)
			{
				Repulsion += (Here - It->GetActorLocation()).GetSafeNormal2D() / Distance;
			}
		}
	}
	if (!Repulsion.IsNearlyZero())
	{
		Flee += Repulsion.GetSafeNormal2D();
	}
	// 2. Away from the squad centre (scattering), 3. towards the nearest heat source.
	const USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
	FVector Centre = Here;
	if (Squad && !Squad->GetMembers().IsEmpty())
	{
		Centre = FVector::ZeroVector;
		for (const AOperativeCharacter* Member : Squad->GetMembers())
		{
			Centre += Member->GetActorLocation();
		}
		Centre /= Squad->GetMembers().Num();
	}
	const float SquadDistance = FVector::Dist2D(Here, Centre) / 100.f;
	if (SquadDistance < 10.f && SquadDistance > 0.01f)
	{
		Flee += (Here - Centre).GetSafeNormal2D() * 0.4f;
	}
	float HeatDistance = 0.f;
	if (const AActor* Heat = FindNearestHeatSource(HeatDistance))
	{
		Flee += (Heat->GetActorLocation() - Here).GetSafeNormal2D() * Config.FleeTowardsHeatBias;
	}
	// 4. Never farther than MaxFleeRadius from the leader (or the squad centre).
	const AOperativeCharacter* Leader = Squad ? Squad->GetLeader() : nullptr;
	const FVector Anchor = Leader && Leader != Operative ? Leader->GetActorLocation() : Centre;
	const FVector ToAnchor = FVector(Anchor.X - Here.X, Anchor.Y - Here.Y, 0.f);
	const float AnchorDistance = ToAnchor.Size() / 100.f;
	if (Config.MaxFleeRadius > 0.f && AnchorDistance > 0.1f)
	{
		const FVector ToAnchorDir = ToAnchor.GetSafeNormal();
		if (AnchorDistance >= Config.MaxFleeRadius)
		{
			const float Away = FVector::DotProduct(Flee, -ToAnchorDir);
			if (Away > 0.f)
			{
				Flee += ToAnchorDir * Away;
			}
			if (AnchorDistance >= Config.MaxFleeRadius * 1.05f)
			{
				Flee += ToAnchorDir * 1.5f;
			}
		}
	}
	Flee.Z = 0.f;
	if (!Flee.IsNearlyZero())
	{
		CachedFleeDirection = Flee.GetSafeNormal();
	}
	else if (CachedFleeDirection.IsNearlyZero())
	{
		CachedFleeDirection = Operative->GetActorForwardVector().GetSafeNormal2D();
	}
	return CachedFleeDirection;
}

float UPanicComponent::GetNearestEnemyDistance() const
{
	const AActor* Owner = GetOwner();
	UWorld* World = GetWorld();
	if (!Owner || !World)
	{
		return -1.f;
	}
	float Nearest = TNumericLimits<float>::Max();
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if (IsLivePanicThreat(*It))
		{
			Nearest = FMath::Min(Nearest, static_cast<float>(FVector::Dist(Owner->GetActorLocation(), It->GetActorLocation())));
		}
	}
	return Nearest < TNumericLimits<float>::Max() ? Nearest / 100.f : -1.f;
}

const AActor* UPanicComponent::FindNearestHeatSource(float& OutDistance) const
{
	const AActor* Owner = GetOwner();
	const AActor* Best = nullptr;
	OutDistance = TNumericLimits<float>::Max();
	for (const TWeakObjectPtr<UHeatSourceComponent>& Source : UHeatSourceComponent::GetAllSources())
	{
		if (Owner && Source.IsValid() && Source->GetWorld() == GetWorld() && Source->IsHeatActive() && Source->GetOwner())
		{
			const float Distance = FVector::Dist(Owner->GetActorLocation(), Source->GetOwner()->GetActorLocation());
			if (Distance < OutDistance)
			{
				OutDistance = Distance;
				Best = Source->GetOwner();
			}
		}
	}
	return Best;
}

int32 UPanicComponent::CountPanickedSquadMembers() const
{
	const USquadSubsystem* Squad = GetWorld() ? GetWorld()->GetSubsystem<USquadSubsystem>() : nullptr;
	int32 Count = 0;
	for (const AOperativeCharacter* Member : Squad ? Squad->GetMembers() : TArray<AOperativeCharacter*>())
	{
		Count += Member->PanicComponent && Member->PanicComponent->IsPanicking() ? 1 : 0;
	}
	return Count;
}

void UPanicComponent::UpdateAura(bool bShow)
{
	const AOperativeCharacter* Operative = GetOperative();
	if (!bShow || !Operative)
	{
		if (Aura)
		{
			Aura->SetActorHiddenInGame(true);
		}
		return;
	}
	if (!Aura && GetWorld())
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Aura = GetWorld()->SpawnActor<ARadiusRingActor>(Params);
	}
	if (Aura)
	{
		// Godot PanicAura: red torus 0.8 - 1.05 m under the feet.
		const FVector Feet = Operative->GetActorLocation() - FVector(0.f, 0.f, Operative->GetSimpleCollisionHalfHeight() - 6.f);
		Aura->ShowRing(Feet, 92.f, FLinearColor(1.f, 0.15f, 0.15f), 25.f);
	}
}

bool UPanicComponent::IsAuraShown() const
{
	return Aura && !Aura->IsHidden();
}

void UPanicComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (Aura)
	{
		Aura->Destroy();
		Aura = nullptr;
	}
	Super::EndPlay(EndPlayReason);
}
