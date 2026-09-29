#include "Characters/RageComponent.h"
#include "Characters/OperativeCharacter.h"
#include "CodexTactics.h"
#include "Combat/HealthComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "UI/FloatingTextSubsystem.h"
#include "UI/GameMessageSubsystem.h"

namespace
{
	bool IsDeadEnemy(const AActor* Enemy)
	{
		if (!IsValid(Enemy))
		{
			return true;
		}
		const UHealthComponent* Health = Enemy->FindComponentByClass<UHealthComponent>();
		return Health && !Health->IsAlive();
	}

	void RagePost(const AOperativeCharacter* Operative, const FString& Text)
	{
		UWorld* World = Operative ? Operative->GetWorld() : nullptr;
		if (UGameMessageSubsystem* Messages = World ? World->GetSubsystem<UGameMessageSubsystem>() : nullptr)
		{
			Messages->PostMessage(Operative->DisplayName, FText::FromString(Text));
		}
	}
}

URageComponent::URageComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

void URageComponent::BeginPlay()
{
	Super::BeginPlay();
}

void URageComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	CombatTimeElapsed += DeltaTime;
	// Godot _clean_expired_enemy_crits (real time).
	const double Now = FPlatformTime::Seconds();
	CritTracker.RemoveAll([this, Now](const FCritRecord& Record)
	{
		return Now - Record.LastTime > Config.CritMemoryWindow || !Record.Enemy.IsValid();
	});
	if (!bRaging)
	{
		return;
	}
	RageTimer -= DeltaTime;
	if (RageTimer <= 0.f)
	{
		ExitRage(TEXT("Время ярости истекло"));
		return;
	}
	ChaoticSwitchTimer -= DeltaTime;
	if (ChaoticSwitchTimer <= 0.f || IsDeadEnemy(ChaoticTarget.Get()))
	{
		ChaoticSwitchTimer = Config.ChaoticSwitchTime;
		ChaoticTarget = FindAnyEnemyInReach();
	}
}

void URageComponent::OnIncomingHit(AActor* Attacker, bool bCrit)
{
	if (!IsValid(Attacker) || bRaging || !bCrit)
	{
		return;
	}
	const double Now = FPlatformTime::Seconds();
	FCritRecord* Record = CritTracker.FindByPredicate([Attacker](const FCritRecord& Entry) { return Entry.Enemy.Get() == Attacker; });
	if (!Record)
	{
		Record = &CritTracker.AddDefaulted_GetRef();
		Record->Enemy = Attacker;
		Record->Crits = 1;
	}
	else
	{
		Record->Crits = Now - Record->LastTime <= Config.CritMemoryWindow ? Record->Crits + 1 : 1;
	}
	Record->LastTime = Now;
	if (Record->Crits < Config.RequiredCrits)
	{
		return;
	}
	const AOperativeCharacter* Operative = Cast<AOperativeCharacter>(GetOwner());
	const UHealthComponent* Health = Operative ? Operative->HealthComponent.Get() : nullptr;
	if (!Health)
	{
		return;
	}
	const float Roll = ForcedRollForTesting >= 0.f ? ForcedRollForTesting : FMath::FRand();
	ForcedRollForTesting = -1.f;
	if (RageRules::CanEnterRage(Config, Health->GetCurrentHealth() / FMath::Max(1.f, Health->GetMaxHealth()), Operative->CurrentClip, Roll, GetCurrentChance()))
	{
		EnterRage(Attacker);
	}
}

void URageComponent::EnterRage(AActor* Offender)
{
	AOperativeCharacter* Operative = Cast<AOperativeCharacter>(GetOwner());
	if (bRaging || !Operative)
	{
		return;
	}
	bRaging = true;
	RageTimer = Config.Duration;
	ChaoticSwitchTimer = 0.f;
	Operative->StopOperative(); // Godot: drops the player's move orders
	ChaoticTarget = IsValid(Offender) ? Offender : FindAnyEnemyInReach();
	UFloatingTextSubsystem::SpawnAboveOperative(Operative, TEXT("🔥 В ЯРОСТИ!"), FLinearColor(1.f, 0.4f, 0.1f));
	RagePost(Operative, FString::Printf(TEXT("🔥 %s: «Ах вы твари! Я вас всех на куски порву!»"), *Operative->DisplayName.ToString()));
	UE_LOG(LogCodexTactics, Log, TEXT("%s enters rage for %.1f s"), *Operative->DisplayName.ToString(), Config.Duration);
	OnRageChanged.Broadcast(true);
}

void URageComponent::ExitRage(const FString& Reason)
{
	if (!bRaging)
	{
		return;
	}
	bRaging = false;
	RageTimer = 0.f;
	ChaoticTarget.Reset();
	const AOperativeCharacter* Operative = Cast<AOperativeCharacter>(GetOwner());
	if (Operative)
	{
		RagePost(Operative, FString::Printf(TEXT("😮‍💨 %s: «Фух... ярость отпустила. Держим строй!»"), *Operative->DisplayName.ToString()));
		UE_LOG(LogCodexTactics, Log, TEXT("%s leaves rage (%s)"), *Operative->DisplayName.ToString(), *Reason);
	}
	OnRageChanged.Broadcast(false);
}

AActor* URageComponent::GetChaoticTarget()
{
	if (IsDeadEnemy(ChaoticTarget.Get()))
	{
		ChaoticTarget = FindAnyEnemyInReach();
	}
	return ChaoticTarget.Get();
}

AActor* URageComponent::FindAnyEnemyInReach() const
{
	const AActor* Owner = GetOwner();
	UWorld* World = GetWorld();
	if (!Owner || !World)
	{
		return nullptr;
	}
	TArray<AActor*> InReach;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if (It->ActorHasTag(FName(TEXT("Enemy"))) && !IsDeadEnemy(*It) && FVector::Dist(Owner->GetActorLocation(), It->GetActorLocation()) <= 2500.f)
		{
			InReach.Add(*It);
		}
	}
	return InReach.IsEmpty() ? nullptr : InReach[FMath::RandRange(0, InReach.Num() - 1)];
}
