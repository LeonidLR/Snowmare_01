#include "Survival/TacticalHeatSubsystem.h"

#include "AI/WorldAIPauseSubsystem.h"
#include "Characters/OperativeCharacter.h"
#include "CodexTactics.h"
#include "Combat/FirePuddleActor.h"
#include "Combat/HealthComponent.h"
#include "Combat/SignalFlareActor.h"
#include "Data/TacticalHeatTuning.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "Subsystems/CodexEventBus.h"
#include "Survival/ColdSurvivalComponent.h"
#include "Survival/TacticalHeatActor.h"
#include "Survival/TacticalHeatTimerComponent.h"
#include "Tactics/TurnBasedCombatSubsystem.h"
#include "UI/FloatingTextSubsystem.h"
#include "UI/GameMessageSubsystem.h"

#define LOCTEXT_NAMESPACE "TacticalHeatSubsystem"

namespace
{
	TArray<TSharedPtr<FJsonValue>> HeatSaveVector(const FVector& Vector)
	{
		return { MakeShared<FJsonValueNumber>(Vector.X), MakeShared<FJsonValueNumber>(Vector.Y), MakeShared<FJsonValueNumber>(Vector.Z) };
	}

	FVector HeatLoadVector(const TSharedPtr<FJsonObject>& Object, const TCHAR* Key)
	{
		const TArray<TSharedPtr<FJsonValue>>* Array = nullptr;
		if (Object.IsValid() && Object->TryGetArrayField(Key, Array) && Array->Num() == 3)
		{
			return FVector((*Array)[0]->AsNumber(), (*Array)[1]->AsNumber(), (*Array)[2]->AsNumber());
		}
		return FVector::ZeroVector;
	}
}

bool UTacticalHeatSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UTacticalHeatSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	if (UTurnBasedCombatSubsystem* TurnBased = InWorld.GetSubsystem<UTurnBasedCombatSubsystem>())
	{
		RoundHandle = TurnBased->OnRoundAdvanced.AddUObject(this, &UTacticalHeatSubsystem::HandleRoundAdvanced);
	}
}

void UTacticalHeatSubsystem::Deinitialize()
{
	if (UWorld* World = GetWorld())
	{
		if (UTurnBasedCombatSubsystem* TurnBased = World->GetSubsystem<UTurnBasedCombatSubsystem>())
		{
			TurnBased->OnRoundAdvanced.Remove(RoundHandle);
		}
	}
	Super::Deinitialize();
}

TStatId UTacticalHeatSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UTacticalHeatSubsystem, STATGROUP_Tickables);
}

bool UTacticalHeatSubsystem::CanAdvanceRealTime() const
{
	const UWorld* World = GetWorld();
	const UGameFlowSubsystem* Flow = World ? World->GetSubsystem<UGameFlowSubsystem>() : nullptr;
	return TacticalHeatRules::ShouldAdvanceRealTime(Flow ? Flow->GetCombatMode() : ECodexCombatMode::None, UWorldAIPauseSubsystem::IsPausedIn(World));
}

void UTacticalHeatSubsystem::Tick(float DeltaTime)
{
	if (DeltaTime <= 0.f || !CanAdvanceRealTime())
	{
		return;
	}
	AdvancedSeconds += DeltaTime;
	// Copy: a burnt-out flare / puddle may destroy itself (and unregister) inside the step.
	const TArray<TWeakObjectPtr<UTacticalHeatTimerComponent>> Timers = UTacticalHeatTimerComponent::GetAll();
	for (const TWeakObjectPtr<UTacticalHeatTimerComponent>& Timer : Timers)
	{
		if (Timer.IsValid() && Timer->GetWorld() == GetWorld())
		{
			Timer->AdvanceRealTime(DeltaTime);
		}
	}
}

void UTacticalHeatSubsystem::HandleRoundAdvanced(int32 Round)
{
	AdvanceRound();
}

void UTacticalHeatSubsystem::AdvanceRound()
{
	const TArray<TWeakObjectPtr<UTacticalHeatTimerComponent>> Timers = UTacticalHeatTimerComponent::GetAll();
	int32 Burning = 0;
	for (const TWeakObjectPtr<UTacticalHeatTimerComponent>& Timer : Timers)
	{
		if (Timer.IsValid() && Timer->GetWorld() == GetWorld() && Timer->IsBurning())
		{
			Timer->AdvanceRound();
			++Burning;
		}
	}
	if (Burning > 0)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("[Heat] turn-based round: %d heat source(s) burnt one round"), Burning);
	}
}

bool UTacticalHeatSubsystem::UseHeatPack(AOperativeCharacter* User, AOperativeCharacter* Target, EHeatPackRefusal* OutRefusal)
{
	auto Refuse = [OutRefusal](EHeatPackRefusal Reason)
	{
		if (OutRefusal)
		{
			*OutRefusal = Reason;
		}
		return false;
	};
	Target = Target ? Target : User;
	if (!User || !Target || User->HeatPacksCount <= 0)
	{
		return Refuse(EHeatPackRefusal::NoPack);
	}
	UColdSurvivalComponent* Cold = Target->ColdSurvival.Get();
	const UHealthComponent* Health = Target->HealthComponent.Get();
	if (!Cold || (Health && !Health->IsAlive()) || !TacticalHeatRules::CanUseHeatPack(Target->ColdLevel, Cold->IsFrostbitten()))
	{
		return Refuse(EHeatPackRefusal::NotNeeded);
	}
	const FTacticalHeatConfig& Config = TacticalHeatTuning::Get();
	UTurnBasedCombatSubsystem* TurnBased = GetWorld()->GetSubsystem<UTurnBasedCombatSubsystem>();
	const bool bTurnBased = TurnBased && TurnBased->IsActive();
	if (bTurnBased)
	{
		if (TurnBased->GetActiveUnit() != User)
		{
			return Refuse(EHeatPackRefusal::NotYourTurn);
		}
		const FTurnUnitState* UnitState = TurnBased->GetUnitState(User);
		if (!UnitState || UnitState->AP < Config.HeatPackActionPoints)
		{
			return Refuse(EHeatPackRefusal::NotEnoughAP);
		}
	}
	const bool bWasFrostbitten = Cold->IsFrostbitten();
	const float ColdBefore = Target->ColdLevel;
	if (!Cold->ApplyHeatPack(Config))
	{
		return Refuse(EHeatPackRefusal::NotNeeded);
	}
	--User->HeatPacksCount;
	if (bTurnBased)
	{
		TurnBased->SpendActiveUnitAP(User, Config.HeatPackActionPoints,
			FString::Printf(TEXT("🔥 %s cracks a heat pack (-%d AP)."), *User->DisplayName.ToString(), Config.HeatPackActionPoints));
	}
	const int32 Removed = FMath::RoundToInt(ColdBefore - Target->ColdLevel);
	UFloatingTextSubsystem::SpawnAboveOperative(Target, bWasFrostbitten ? FString::Printf(TEXT("🔥 HEAT PACK -%d%% COLD - BACK ON HIS FEET"), Removed)
		: FString::Printf(TEXT("🔥 HEAT PACK -%d%% COLD"), Removed), FLinearColor(1.f, 0.62f, 0.2f));
	if (UGameMessageSubsystem* Messages = GetWorld()->GetSubsystem<UGameMessageSubsystem>())
	{
		const FText Line = User == Target
			? FText::Format(LOCTEXT("PackSelf", "🔥 Heat pack cracked: cold -{0}%, heat buffer for {1} s (packs left: x{2})."),
				Removed, FMath::RoundToInt(Config.HeatPackBufferSeconds), User->HeatPacksCount)
			: FText::Format(LOCTEXT("PackMate", "🔥 Heat pack on {0}: cold -{1}%, heat buffer for {2} s (packs left: x{3})."),
				Target->DisplayName, Removed, FMath::RoundToInt(Config.HeatPackBufferSeconds), User->HeatPacksCount);
		Messages->PostMessage(User->DisplayName, Line);
		if (bWasFrostbitten)
		{
			Messages->PostMessage(Target->DisplayName, LOCTEXT("Thawed", "I can feel my legs again... I can move!"));
		}
	}
	if (UCodexEventBus* Bus = UCodexEventBus::Get(this))
	{
		Bus->OnItemUsed.Broadcast(Target, TEXT("HEAT_PACK"));
	}
	UE_LOG(LogCodexTactics, Display, TEXT("[Heat] %s used a heat pack on %s: cold %.1f -> %.1f, frostbite %d -> 0, buffer %.0f s"),
		*User->DisplayName.ToString(), *Target->DisplayName.ToString(), ColdBefore, Target->ColdLevel, bWasFrostbitten ? 1 : 0,
		Cold->GetHeatBufferSecondsLeft());
	if (OutRefusal)
	{
		*OutRefusal = EHeatPackRefusal::None;
	}
	return true;
}

bool UTacticalHeatSubsystem::IsRevealedByFlare(const UWorld* World, const FVector& Location)
{
	if (!World)
	{
		return false;
	}
	const FTacticalHeatConfig& Config = TacticalHeatTuning::Get();
	for (TActorIterator<ASignalFlareActor> It(World); It; ++It)
	{
		if (TacticalHeatRules::IsRevealedByFlare(Config, It->IsBurning(), FVector::Dist(It->GetActorLocation(), Location)))
		{
			return true;
		}
	}
	return false;
}

TArray<TSharedPtr<FJsonValue>> UTacticalHeatSubsystem::SaveLooseSources() const
{
	TArray<TSharedPtr<FJsonValue>> Entries;
	for (TActorIterator<ATacticalHeatActor> It(GetWorld()); It; ++It)
	{
		const ATacticalHeatActor* Source = *It;
		if (!IsValid(Source) || Source->IsActorBeingDestroyed() || !Source->ShouldSave())
		{
			continue;
		}
		TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("class"), Source->GetClass()->GetPathName());
		Entry->SetArrayField(TEXT("pos"), HeatSaveVector(Source->GetSaveLocation()));
		Entry->SetBoolField(TEXT("burning"), Source->GetSaveBurning());
		Entry->SetBoolField(TEXT("burnt_out"), Source->IsBurntOut());
		Entry->SetNumberField(TEXT("time_left"), Source->GetSaveTimeLeft());
		Entry->SetNumberField(TEXT("round_seconds"), Source->GetSaveRoundSeconds());
		Entries.Add(MakeShared<FJsonValueObject>(Entry));
	}
	// Order-independent for the round-trip check: sorted by place.
	Entries.Sort([](const TSharedPtr<FJsonValue>& A, const TSharedPtr<FJsonValue>& B)
	{
		const FVector PA = HeatLoadVector(A->AsObject(), TEXT("pos"));
		const FVector PB = HeatLoadVector(B->AsObject(), TEXT("pos"));
		return PA.X != PB.X ? PA.X < PB.X : PA.Y < PB.Y;
	});
	return Entries;
}

void UTacticalHeatSubsystem::LoadLooseSources(const TArray<TSharedPtr<FJsonValue>>& Entries)
{
	UWorld* World = GetWorld();
	for (TActorIterator<ATacticalHeatActor> It(World); It; ++It)
	{
		It->Destroy();
	}
	for (const TSharedPtr<FJsonValue>& Value : Entries)
	{
		const TSharedPtr<FJsonObject> Entry = Value.IsValid() ? Value->AsObject() : nullptr;
		FString ClassPath;
		if (!Entry.IsValid() || !Entry->TryGetStringField(TEXT("class"), ClassPath))
		{
			continue;
		}
		UClass* Class = LoadClass<ATacticalHeatActor>(nullptr, *ClassPath);
		if (!Class)
		{
			continue;
		}
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		ATacticalHeatActor* Spawned = World->SpawnActor<ATacticalHeatActor>(Class, HeatLoadVector(Entry, TEXT("pos")), FRotator::ZeroRotator, Params);
		if (!Spawned)
		{
			continue;
		}
		bool bBurning = false;
		bool bBurntOut = false;
		double TimeLeft = 0.0;
		double RoundSeconds = 0.0;
		Entry->TryGetBoolField(TEXT("burning"), bBurning);
		Entry->TryGetBoolField(TEXT("burnt_out"), bBurntOut);
		Entry->TryGetNumberField(TEXT("time_left"), TimeLeft);
		Entry->TryGetNumberField(TEXT("round_seconds"), RoundSeconds);
		Spawned->RestoreHeatState(bBurning, bBurntOut, static_cast<float>(TimeLeft), static_cast<float>(RoundSeconds));
	}
}

#undef LOCTEXT_NAMESPACE
