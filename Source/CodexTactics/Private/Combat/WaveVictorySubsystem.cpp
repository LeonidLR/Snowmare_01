#include "Combat/WaveVictorySubsystem.h"
#include "Characters/EnemyCharacter.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/WaveSubsystem.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "Interactables/DeployableActor.h"
#include "Interactables/DeployableRules.h"
#include "UI/GameMessageSubsystem.h"

#define LOCTEXT_NAMESPACE "WaveVictorySubsystem"

void UWaveVictorySubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	if (UGameFlowSubsystem* Flow = InWorld.GetSubsystem<UGameFlowSubsystem>())
	{
		Flow->OnGameFlowChanged.AddDynamic(this, &UWaveVictorySubsystem::HandleGameFlowChanged);
		LastPhase = Flow->GetPhase();
	}
}

void UWaveVictorySubsystem::RegisterEnemyKill(EEnemyArchetype Type, const FString& Source)
{
	const USquadSubsystem* Squad = GetWorld()->GetSubsystem<USquadSubsystem>();
	const AOperativeCharacter* Leader = Squad ? Squad->GetLeader() : nullptr;
	KillStatsRules::RegisterKill(KillStats, Type, Source, Leader ? Leader->DisplayName.ToString() : FString());
}

FString UWaveVictorySubsystem::GetVictoryTitle() const
{
	const UGameFlowSubsystem* Flow = GetWorld()->GetSubsystem<UGameFlowSubsystem>();
	const int32 Wave = Flow ? Flow->GetWaveIndex() : 1;
	const int32 Total = Flow ? Flow->GetConfig().TotalWaves : 1;
	return Wave >= Total ? FString::Printf(TEXT("🏆 ПОЛНАЯ ПОБЕДА! ВСЕ %d ВОЛН ОТРАЖЕНЫ! 🏆"), Total)
		: FString::Printf(TEXT("🎉 ВОЛНА %d ОТРАЖЕНА! 🎉"), Wave);
}

FString UWaveVictorySubsystem::GetVictorySubtitle() const
{
	const UGameFlowSubsystem* Flow = GetWorld()->GetSubsystem<UGameFlowSubsystem>();
	const int32 Wave = Flow ? Flow->GetWaveIndex() : 1;
	const int32 Total = Flow ? Flow->GetConfig().TotalWaves : 1;
	return Wave >= Total ? FString(TEXT("Карантинный рубеж КПП полностью зачищен от ледяных орд! Отряд выстоял!"))
		: FString::Printf(TEXT("Все ледяные твари в волне %d уничтожены! Оборона КПП устояла."), Wave);
}

FString UWaveVictorySubsystem::GetNextButtonText() const
{
	const UGameFlowSubsystem* Flow = GetWorld()->GetSubsystem<UGameFlowSubsystem>();
	const int32 Wave = Flow ? Flow->GetWaveIndex() : 1;
	const int32 Total = Flow ? Flow->GetConfig().TotalWaves : 1;
	return Wave >= Total ? FString(TEXT("🗺️ Завершить бой и продолжить исследование"))
		: FString::Printf(TEXT("⚔️ Запустить следующую волну (%d/%d)"), Wave + 1, Total);
}

void UWaveVictorySubsystem::ContinueAfterWave()
{
	UGameFlowSubsystem* Flow = GetWorld()->GetSubsystem<UGameFlowSubsystem>();
	if (!Flow || Flow->GetPhase() != ECodexGamePhase::WaveCleared)
	{
		return;
	}
	// The planned tactical orders were dropped when the wave ended; the flow restores the pause charges and starts the
	// rest countdown (or PostCombat after the last wave, handled below).
	Flow->AdvanceAfterWave();
}

FVector UWaveVictorySubsystem::GetPrepStation(const AOperativeCharacter* Member) const
{
	const FVector* Station = PrepStations.Find(const_cast<AOperativeCharacter*>(Member));
	return Station ? *Station : (Member ? Member->GetActorLocation() : FVector::ZeroVector);
}

void UWaveVictorySubsystem::RecordPrepStations(bool bOverwrite)
{
	const USquadSubsystem* Squad = GetWorld()->GetSubsystem<USquadSubsystem>();
	for (AOperativeCharacter* Member : Squad ? Squad->GetMembers() : TArray<AOperativeCharacter*>())
	{
		if (bOverwrite || !PrepStations.Contains(Member))
		{
			PrepStations.Add(Member, Member->GetActorLocation());
		}
	}
}

void UWaveVictorySubsystem::HandleGameFlowChanged(ECodexGamePhase Phase, ECodexCombatMode CombatMode)
{
	if (Phase == LastPhase)
	{
		return;
	}
	const ECodexGamePhase Previous = LastPhase;
	LastPhase = Phase;
	const UGameFlowSubsystem* Flow = GetWorld()->GetSubsystem<UGameFlowSubsystem>();
	if (Previous == ECodexGamePhase::Cutscene && Phase == ECodexGamePhase::Preparation)
	{
		// Godot _end_cutscene_and_start_pause: initial_prep_station = where each operative stands now.
		RecordPrepStations(true);
	}
	else if (Phase == ECodexGamePhase::WaveCleared && Flow)
	{
		const int32 Wave = Flow->GetWaveIndex();
		const int32 Total = Flow->GetConfig().TotalWaves;
		if (UGameMessageSubsystem* Messages = GetWorld()->GetSubsystem<UGameMessageSubsystem>())
		{
			if (Wave >= Total)
			{
				Messages->PostMessage(LOCTEXT("HQ", "ШТАБ"),
					FText::Format(LOCTEXT("AllWaves", "Поздравляем отряд! Все {0} волн отбиты, территория КПП освобождена!"), Total));
			}
			else
			{
				Messages->PostMessage(LOCTEXT("Commander", "Командир"),
					FText::Format(LOCTEXT("WaveDone", "Отличная работа, отряд! Волна {0} зачищена. Перегруппироваться!"), Wave));
			}
		}
	}
	else if (Phase == ECodexGamePhase::PostCombat)
	{
		StartPostCombatSequence();
	}
}

void UWaveVictorySubsystem::StartPostCombatSequence()
{
	UWorld* World = GetWorld();
	// Godot: every enemy still in the scene goes.
	if (UWaveSubsystem* Waves = World->GetSubsystem<UWaveSubsystem>())
	{
		Waves->ClearAllEnemies();
	}
	for (TActorIterator<AEnemyCharacter> It(World); It; ++It)
	{
		It->Destroy();
	}

	// The squad back at its preparation spots, orders dropped; the commander leads the exploration formation.
	USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
	RecordPrepStations(false);
	for (AOperativeCharacter* Member : Squad ? Squad->GetMembers() : TArray<AOperativeCharacter*>())
	{
		Member->StopOperative();
		Member->ClearPlannedTargetedShots();
		Member->TeleportTo(GetPrepStation(Member), Member->GetActorRotation(), false, true);
	}
	if (Squad)
	{
		for (AOperativeCharacter* Member : Squad->GetMembers())
		{
			if (Member->SquadRole == EOperativeRole::Commander)
			{
				Squad->SetLeader(Member);
				break;
			}
		}
	}
	RecoverDeployables();
	UE_LOG(LogCodexTactics, Log, TEXT("Post-combat: squad returned, deployables recovered"));
}

void UWaveVictorySubsystem::RecoverDeployables()
{
	UWorld* World = GetWorld();
	const USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
	AOperativeCharacter* Leader = Squad ? Squad->GetLeader() : nullptr;
	AOperativeCharacter* Commander = nullptr;
	AOperativeCharacter* Engineer = nullptr;
	AOperativeCharacter* Medic = nullptr;
	for (AOperativeCharacter* Member : Squad ? Squad->GetMembers() : TArray<AOperativeCharacter*>())
	{
		if (Member->SquadRole == EOperativeRole::Commander)
		{
			Commander = Member;
		}
		else if (Member->SquadRole == EOperativeRole::Engineer)
		{
			Engineer = Member;
		}
		else if (Member->SquadRole == EOperativeRole::MedicSapper)
		{
			Medic = Member;
		}
	}
	auto Count = [](AOperativeCharacter* Member, EDeployableType Type) -> int32*
	{
		if (!Member)
		{
			return nullptr;
		}
		return Type == EDeployableType::Turret ? &Member->TurretsCount
			: (Type == EDeployableType::Barricade ? &Member->BarricadesCount : &Member->MinesCount);
	};

	int32 Recovered[3] = { 0, 0, 0 };
	TArray<ADeployableActor*> Found;
	for (TActorIterator<ADeployableActor> It(World); It; ++It)
	{
		if (It->bDeployable && !It->IsActorBeingDestroyed())
		{
			Found.Add(*It);
		}
	}
	for (ADeployableActor* Deployable : Found)
	{
		const EDeployableType Type = Deployable->GetDeployableType();
		// Godot order: turrets leader, commander, engineer, medic (else the commander); barricades leader, engineer,
		// commander, medic (else the engineer); mines leader, medic, engineer, commander (else the medic).
		TArray<AOperativeCharacter*> Order;
		AOperativeCharacter* Owner = nullptr;
		switch (Type)
		{
		case EDeployableType::Turret: Order = { Leader, Commander, Engineer, Medic }; Owner = Commander; break;
		case EDeployableType::Barricade: Order = { Leader, Engineer, Commander, Medic }; Owner = Engineer; break;
		default: Order = { Leader, Medic, Engineer, Commander }; Owner = Medic; break;
		}
		TArray<int32> Carried;
		for (AOperativeCharacter* Member : Order)
		{
			const int32* Value = Count(Member, Type);
			Carried.Add(Value ? *Value : -1);
		}
		const int32 Pick = DeployableRules::PickRecoveryRecipient(Carried, DeployableRules::GetMaxCarried(Type));
		if (int32* Target = Count(Pick != INDEX_NONE ? Order[Pick] : Owner, Type))
		{
			++*Target;
		}
		++Recovered[static_cast<int32>(Type)];
		Deployable->Destroy();
	}

	TArray<FString> Parts;
	if (Recovered[0] > 0)
	{
		Parts.Add(FString::Printf(TEXT("турелей: %d"), Recovered[0]));
	}
	if (Recovered[1] > 0)
	{
		Parts.Add(FString::Printf(TEXT("баррикад: %d"), Recovered[1]));
	}
	if (Recovered[2] > 0)
	{
		Parts.Add(FString::Printf(TEXT("мин: %d"), Recovered[2]));
	}
	if (!Parts.IsEmpty())
	{
		if (UGameMessageSubsystem* Messages = World->GetSubsystem<UGameMessageSubsystem>())
		{
			Messages->PostMessage(LOCTEXT("Engineer", "Инженер"), FText::FromString(FString::Printf(
				TEXT("🛠️ Уцелевшие укрепления демонтированы и возвращены в снаряжение (%s)."), *FString::Join(Parts, TEXT(", ")))));
		}
	}
}

#undef LOCTEXT_NAMESPACE
