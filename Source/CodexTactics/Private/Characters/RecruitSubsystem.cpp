#include "Characters/RecruitSubsystem.h"
#include "UI/FloatingTextSubsystem.h"
#include "Camera/TacticalCameraPawn.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/HealthComponent.h"
#include "Combat/WaveSubsystem.h"
#include "Core/CodexTacticsGameMode.h"
#include "Core/MissionRules.h"
#include "Core/MissionSubsystem.h"
#include "Data/DialogueSequenceAsset.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "Kismet/GameplayStatics.h"
#include "Survival/ColdSurvivalComponent.h"
#include "TimerManager.h"
#include "UI/DialogueSubsystem.h"
#include "UI/GameMessageSubsystem.h"

const FName URecruitSubsystem::SpawnTag(TEXT("SusaninSpawn"));

namespace
{
	void RecruitPost(const UWorld* World, const TCHAR* Speaker, const FString& Text)
	{
		if (UGameMessageSubsystem* Messages = World ? World->GetSubsystem<UGameMessageSubsystem>() : nullptr)
		{
			Messages->PostMessage(FText::FromString(Speaker), FText::FromString(Text));
		}
	}
}

void URecruitSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Collection.InitializeDependency(UWaveSubsystem::StaticClass());
}

void URecruitSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	if (UWaveSubsystem* Waves = InWorld.GetSubsystem<UWaveSubsystem>())
	{
		Waves->OnWaveStarted.AddDynamic(this, &URecruitSubsystem::HandleWaveStarted);
	}
}

bool URecruitSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId URecruitSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(URecruitSubsystem, STATGROUP_Tickables);
}

void URecruitSubsystem::HandleWaveStarted(int32 WaveIndex, int32 TotalEnemies)
{
	UWaveSubsystem* Waves = GetWorld()->GetSubsystem<UWaveSubsystem>();
	if (!Waves || WaveIndex != Waves->GetSusaninRescueWave() || bRescueTriggered)
	{
		return;
	}
	// Godot: randf_range(5, 8) s after the wave starts (3 s in headless test mode).
	const float Delay = Waves->bInstantRandomEvents ? 3.f : FMath::FRandRange(5.f, 8.f);
	TWeakObjectPtr<URecruitSubsystem> WeakThis(this);
	FTimerHandle Handle;
	GetWorld()->GetTimerManager().SetTimer(Handle, FTimerDelegate::CreateLambda([WeakThis]()
	{
		URecruitSubsystem* Self = WeakThis.Get();
		UWaveSubsystem* WaveSystem = Self ? Self->GetWorld()->GetSubsystem<UWaveSubsystem>() : nullptr;
		if (!WaveSystem || !WaveSystem->IsWaveActive() || Self->bRescueTriggered)
		{
			return;
		}
		WaveSystem->RunOrDeferRandomEvent([WeakThis]()
		{
			if (WeakThis.IsValid())
			{
				WeakThis->TriggerRescueEvent();
			}
		});
	}), Delay, false);
}

AOperativeCharacter* URecruitSubsystem::GetOrSpawnSusanin()
{
	if (Susanin.IsValid())
	{
		return Susanin.Get();
	}
	UWorld* World = GetWorld();
	ACodexTacticsGameMode* GameMode = World ? World->GetAuthGameMode<ACodexTacticsGameMode>() : nullptr;
	if (!GameMode)
	{
		return nullptr;
	}
	FVector Location = FVector::ZeroVector;
	FRotator Facing = FRotator::ZeroRotator;
	TArray<AActor*> Points;
	UGameplayStatics::GetAllActorsWithTag(World, SpawnTag, Points);
	if (!Points.IsEmpty())
	{
		Location = Points[0]->GetActorLocation() + FVector(0.f, 0.f, 100.f);
		Facing = FRotator(0.f, Points[0]->GetActorRotation().Yaw, 0.f);
	}
	else if (const AOperativeCharacter* Leader = World->GetSubsystem<USquadSubsystem>()->GetLeader())
	{
		Location = Leader->GetActorLocation() + Leader->GetActorRightVector() * 600.f; // no marker in the level
	}
	AOperativeCharacter* Spawned = GameMode->SpawnOperative(GameMode->RecruitSusanin, 3, Location, Facing, false);
	if (Spawned)
	{
		// Godot recruit_susanin.gd _ready: civilian kit — one can of food, no medkits or engineering items.
		Spawned->MedkitsCount = 0;
		Spawned->CannedFoodCount = 1;
		Spawned->TurretsCount = 0;
		Spawned->BarricadesCount = 0;
		Spawned->MinesCount = 0;
	}
	Susanin = Spawned;
	UE_LOG(LogCodexTactics, Log, TEXT("Susanin spawned at (%.0f, %.0f, %.0f)"), Location.X, Location.Y, Location.Z);
	return Spawned;
}

bool URecruitSubsystem::IsSusaninRecruited() const
{
	return Susanin.IsValid() && Susanin->bRecruited;
}

bool URecruitSubsystem::IsRecruit(const AActor* Actor) const
{
	return Actor && Actor == Susanin.Get() && !Susanin->bRecruited;
}

void URecruitSubsystem::TriggerRescueEvent()
{
	UWaveSubsystem* Waves = GetWorld()->GetSubsystem<UWaveSubsystem>();
	const UGameFlowSubsystem* Flow = GetWorld()->GetSubsystem<UGameFlowSubsystem>();
	if (Waves && Flow && Flow->GetCombatMode() == ECodexCombatMode::TurnBased)
	{
		TWeakObjectPtr<URecruitSubsystem> WeakThis(this);
		Waves->RunOrDeferRandomEvent([WeakThis]() { if (WeakThis.IsValid()) { WeakThis->TriggerRescueEvent(); } });
		return;
	}
	AOperativeCharacter* Recruit = GetOrSpawnSusanin();
	if (!Recruit || !Recruit->HealthComponent || !Recruit->HealthComponent->IsAlive() || Recruit->bRecruited)
	{
		return;
	}
	bRescueTriggered = true;
	BeginNarrativePause();
	FocusCamera(Recruit);
	// Godot trigger_cold_distress.
	bColdDistress = true;
	Recruit->ColdLevel = 85.f;
	UFloatingTextSubsystem::SpawnAboveOperative(Recruit, TEXT("🥶 FREEZING!"), FLinearColor(0.4f, 0.8f, 1.f));

	if (UDialogueSubsystem* Dialogue = GetWorld()->GetSubsystem<UDialogueSubsystem>())
	{
		Dialogue->StartDialogue(MakeDialogue(TEXT("Distress signal: Ivan Susanin"), TEXT("Hold on! We're coming! ▶"),
			TEXT("Hey, help me, please, I'm freezing!")), FSimpleDelegate::CreateUObject(this, &URecruitSubsystem::HandleDistressDialogueFinished));
	}
}

void URecruitSubsystem::HandleDistressDialogueFinished()
{
	EndNarrativePause();
	if (const AOperativeCharacter* Leader = GetWorld()->GetSubsystem<USquadSubsystem>()->GetLeader())
	{
		FocusCamera(const_cast<AOperativeCharacter*>(Leader));
	}
	RecruitPost(GetWorld(), TEXT("HQ"), TEXT("⚠️ Susanin is freezing at the line! Reach him with any operative to rescue him and take him into the squad!"));
	if (UMissionSubsystem* Mission = GetWorld()->GetSubsystem<UMissionSubsystem>())
	{
		Mission->SetObjective(FText::FromString(TEXT("Rescue Susanin: reach him with a squad operative!")));
	}
}

void URecruitSubsystem::Tick(float DeltaTime)
{
	AOperativeCharacter* Recruit = Susanin.Get();
	if (!Recruit || Recruit->bRecruited || !bColdDistress || bRecruitmentDialogueActive)
	{
		return;
	}
	// Godot enter_warm_zone / _check_cold_distress_recovery: a heat source rescues him at once.
	if (Recruit->ColdSurvival && Recruit->ColdSurvival->IsNearHeatSource())
	{
		RecruitIntoSquad();
		return;
	}
	// Godot _physics_process: any living operative within interaction_distance starts the recruitment dialogue.
	const UDialogueSubsystem* Dialogue = GetWorld()->GetSubsystem<UDialogueSubsystem>();
	if (Dialogue && Dialogue->IsDialogueOpen())
	{
		return;
	}
	for (AOperativeCharacter* Member : GetWorld()->GetSubsystem<USquadSubsystem>()->GetMembers())
	{
		if (Member->HealthComponent && Member->HealthComponent->IsAlive()
			&& FVector::Dist(Member->GetActorLocation(), Recruit->GetActorLocation()) <= RescueDistance)
		{
			StartRecruitmentDialogue(Member);
			return;
		}
	}
}

void URecruitSubsystem::HandleRecruitClicked(bool bSprint)
{
	AOperativeCharacter* Recruit = Susanin.Get();
	USquadSubsystem* Squad = GetWorld()->GetSubsystem<USquadSubsystem>();
	AOperativeCharacter* Leader = Squad ? Squad->GetLeader() : nullptr;
	if (!Recruit || Recruit->bRecruited || !Leader)
	{
		return;
	}
	AOperativeCharacter* Nearest = Leader;
	float NearestDistance = FVector::Dist(Leader->GetActorLocation(), Recruit->GetActorLocation());
	for (AOperativeCharacter* Member : Squad->GetMembers())
	{
		const float Distance = FVector::Dist(Member->GetActorLocation(), Recruit->GetActorLocation());
		if (Member->HealthComponent && Member->HealthComponent->IsAlive() && Distance < NearestDistance)
		{
			Nearest = Member;
			NearestDistance = Distance;
		}
	}
	if (NearestDistance <= RescueDistance)
	{
		StartRecruitmentDialogue(Nearest);
		return;
	}
	// Godot _get_approach_position_for_object: 1.5 m short of him on the leader's side.
	const FVector Away = (Leader->GetActorLocation() - Recruit->GetActorLocation()).GetSafeNormal2D();
	Leader->OrderMoveTo(Recruit->GetActorLocation() + Away * 150.f, bSprint);
}

void URecruitSubsystem::StartRecruitmentDialogue(AOperativeCharacter* Rescuer)
{
	AOperativeCharacter* Recruit = Susanin.Get();
	if (!Recruit || Recruit->bRecruited || bRecruitmentDialogueActive)
	{
		return;
	}
	if (Rescuer)
	{
		const FVector ToRescuer = (Rescuer->GetActorLocation() - Recruit->GetActorLocation()).GetSafeNormal2D();
		if (!ToRescuer.IsNearlyZero())
		{
			Recruit->SetActorRotation(ToRescuer.Rotation());
		}
	}
	bRecruitmentDialogueActive = true;
	BeginNarrativePause();
	TWeakObjectPtr<URecruitSubsystem> WeakThis(this);
	UDialogueSequenceAsset* Sequence = MakeDialogue(TEXT("Joining the squad"), TEXT("🤝 Take into the squad ▶"),
		TEXT("Thank you for coming to my rescue! Take me with you, I know these parts and I'll help the squad hold out!"));
	UDialogueSubsystem* Dialogue = GetWorld()->GetSubsystem<UDialogueSubsystem>();
	FSimpleDelegate OnFinish = FSimpleDelegate::CreateLambda([WeakThis]()
	{
		if (URecruitSubsystem* Self = WeakThis.Get())
		{
			Self->bRecruitmentDialogueActive = false;
			Self->EndNarrativePause();
			Self->RecruitIntoSquad();
		}
	});
	if (Dialogue)
	{
		Dialogue->StartDialogue(Sequence, OnFinish);
	}
	else
	{
		OnFinish.Execute();
	}
}

void URecruitSubsystem::RecruitIntoSquad(bool bSilent)
{
	AOperativeCharacter* Recruit = Susanin.Get();
	if (!Recruit || Recruit->bRecruited)
	{
		return;
	}
	Recruit->bRecruited = true;
	bColdDistress = false;
	Recruit->ColdLevel = 0.f;
	GetWorld()->GetSubsystem<USquadSubsystem>()->RegisterOperative(Recruit); // Godot add_to_group("squad") + assign_formation_slots
	if (bSilent)
	{
		return;
	}
	RecruitPost(GetWorld(), TEXT("Ivan Susanin"), TEXT("Thank you, lads! I'm with you. I'll stay close!"));
	UFloatingTextSubsystem::SpawnAboveOperative(Recruit, TEXT("🤝 JOINED THE SQUAD"), FLinearColor(0.3f, 1.f, 0.5f));
	RecruitPost(GetWorld(), TEXT("HQ"), TEXT("✅ Ivan Susanin has joined the squad! [Key 4 - select]"));
	const UWaveSubsystem* Waves = GetWorld()->GetSubsystem<UWaveSubsystem>();
	UMissionSubsystem* Mission = GetWorld()->GetSubsystem<UMissionSubsystem>();
	if (Mission && Waves && Waves->IsWaveActive())
	{
		Mission->SetObjective(MissionRules::GetWaveObjective(Waves->GetCurrentWaveIndex(), Waves->GetTotalWaveEnemies()));
	}
}

void URecruitSubsystem::RestoreRecruited(bool bRecruited)
{
	if (bRecruited)
	{
		if (GetOrSpawnSusanin())
		{
			bRescueTriggered = true;
			RecruitIntoSquad(true);
		}
		return;
	}
	if (AOperativeCharacter* Recruit = Susanin.Get(); Recruit && Recruit->bRecruited)
	{
		Recruit->bRecruited = false;
		GetWorld()->GetSubsystem<USquadSubsystem>()->UnregisterOperative(Recruit);
	}
}

void URecruitSubsystem::BeginNarrativePause()
{
	// Godot is_narrative_pause + Engine.time_scale = 0 (the engine clamps 0 to a near-stop).
	UGameplayStatics::SetGlobalTimeDilation(GetWorld(), 0.f);
}

void URecruitSubsystem::EndNarrativePause()
{
	// Godot: time_scale back to 1 unless the tactical pause is on — the flow's dilation for its current state.
	if (UGameFlowSubsystem* Flow = GetWorld()->GetSubsystem<UGameFlowSubsystem>())
	{
		Flow->ApplyTimeDilation();
	}
}

void URecruitSubsystem::FocusCamera(AActor* Target)
{
	if (ATacticalCameraPawn* Camera = Cast<ATacticalCameraPawn>(UGameplayStatics::GetPlayerPawn(GetWorld(), 0)))
	{
		Camera->SetFollowTarget(Target);
	}
}

UDialogueSequenceAsset* URecruitSubsystem::MakeDialogue(const FString& Title, const FString& FinishButton, const FString& Text)
{
	UDialogueSequenceAsset* Sequence = NewObject<UDialogueSequenceAsset>(this);
	Sequence->Title = Title;
	Sequence->CustomFinishButtonText = FinishButton;
	FDialogueLine Line;
	Line.SpeakerName = TEXT("Ivan Susanin");
	Line.Text = Text;
	Sequence->Lines.Add(Line);
	Dialogues.Add(Sequence);
	return Sequence;
}
