#include "Level/BridgeCrossingVolume.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Components/BoxComponent.h"
#include "Core/PlayerCommands.h"
#include "Engine/World.h"
#include "Level/LevelObstacleRules.h"
#include "UI/GameMessageSubsystem.h"

#define LOCTEXT_NAMESPACE "BridgeCrossingVolume"

ABridgeCrossingVolume::ABridgeCrossingVolume()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false; // ticks only while somebody is on / at the span
	Span = CreateDefaultSubobject<UBoxComponent>(TEXT("Span"));
	SetRootComponent(Span);
	Span->SetBoxExtent(FVector(300.f, 100.f, 125.f)); // 6 x 2 x 2.5 m: resize per instance, X along the bridge
	Span->SetCollisionProfileName(TEXT("OverlapAllDynamic"));
	Span->SetGenerateOverlapEvents(true);
	Span->SetCanEverAffectNavigation(false);
	Span->ShapeColor = FColor(120, 200, 255); // ice blue = a crossing
	Span->SetHiddenInGame(true);
	RefusalLine = LOCTEXT("Refusal", "The bridge won't hold two! Wait for him to get across.");
	RefusalCooldownSeconds = BridgeRules::DefaultRefusalCooldownSeconds;
}

void ABridgeCrossingVolume::BeginPlay()
{
	Super::BeginPlay();
	Span->OnComponentBeginOverlap.AddDynamic(this, &ABridgeCrossingVolume::HandleBeginOverlap);
}

void ABridgeCrossingVolume::HandleBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp,
	int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	if (IsSquadOperative(OtherActor))
	{
		SetActorTickEnabled(true);
		UpdateTraffic();
	}
}

bool ABridgeCrossingVolume::IsSquadOperative(const AActor* Actor) const
{
	const AOperativeCharacter* Operative = Cast<AOperativeCharacter>(Actor);
	if (!Operative)
	{
		return false;
	}
	const USquadSubsystem* Squad = GetWorld() ? GetWorld()->GetSubsystem<USquadSubsystem>() : nullptr;
	return Squad && Squad->GetMembers().Contains(Operative);
}

bool ABridgeCrossingVolume::IsOccupant(const AOperativeCharacter* Operative) const
{
	return Occupants.ContainsByPredicate([Operative](const FOccupant& Entry) { return Entry.Operative.Get() == Operative; });
}

int32 ABridgeCrossingVolume::FindWaiting(const AOperativeCharacter* Operative) const
{
	return Waiting.IndexOfByPredicate([Operative](const FWaiting& Entry) { return Entry.Operative.Get() == Operative; });
}

void ABridgeCrossingVolume::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	UpdateTraffic();
	if (Occupants.IsEmpty() && Waiting.IsEmpty())
	{
		SetActorTickEnabled(false);
	}
}

void ABridgeCrossingVolume::UpdateTraffic()
{
	TArray<AActor*> Inside;
	Span->GetOverlappingActors(Inside, AOperativeCharacter::StaticClass());
	TArray<AOperativeCharacter*> OnSpan;
	for (AActor* Actor : Inside)
	{
		if (IsSquadOperative(Actor))
		{
			OnSpan.Add(static_cast<AOperativeCharacter*>(Actor));
		}
	}
	// Occupants who left the span: a crossing when they came out on the other side, else they stepped back.
	for (int32 Index = Occupants.Num() - 1; Index >= 0; --Index)
	{
		AOperativeCharacter* Operative = Occupants[Index].Operative.Get();
		if (!Operative || !OnSpan.Contains(Operative))
		{
			Release(Operative, /*bLeftSpan*/ true);
		}
	}
	// Waiting operatives who walked away (a new order elsewhere): free them.
	for (int32 Index = Waiting.Num() - 1; Index >= 0; --Index)
	{
		AOperativeCharacter* Operative = Waiting[Index].Operative.Get();
		if (!Operative || !OnSpan.Contains(Operative))
		{
			if (Operative)
			{
				Operative->SetTrafficHold(this, false);
			}
			Waiting.RemoveAt(Index);
		}
	}
	// Everyone on the span: occupants stay, the rest are admitted while there is room, else stopped at the edge.
	for (AOperativeCharacter* Operative : OnSpan)
	{
		switch (BridgeRules::Evaluate(Occupants.Num(), Capacity, IsOccupant(Operative)))
		{
		case BridgeRules::EBridgeEntry::Admit:
			Admit(Operative);
			break;
		case BridgeRules::EBridgeEntry::Refuse:
			Refuse(Operative);
			break;
		default:
			break;
		}
	}
}

void ABridgeCrossingVolume::Admit(AOperativeCharacter* Operative)
{
	const int32 WaitingIndex = FindWaiting(Operative);
	if (WaitingIndex != INDEX_NONE)
	{
		Waiting.RemoveAt(WaitingIndex);
	}
	Operative->SetTrafficHold(this, false);
	FOccupant& Occupant = Occupants.AddDefaulted_GetRef();
	Occupant.Operative = Operative;
	Occupant.EntryCoordinate = BridgeRules::SpanCoordinate(GetActorTransform(), Operative->GetActorLocation());
	UE_LOG(LogCodexTactics, Display, TEXT("Bridge %s: %s steps on (span %.0f cm, %d / %d)"), *GetName(), *Operative->DisplayName.ToString(),
		Occupant.EntryCoordinate, Occupants.Num(), Capacity);
	GameplayEvents::Raise(this, GameplayEvents::BridgeEntered, Operative);
}

void ABridgeCrossingVolume::Refuse(AOperativeCharacter* Operative)
{
	const float Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
	int32 WaitingIndex = FindWaiting(Operative);
	if (WaitingIndex == INDEX_NONE)
	{
		WaitingIndex = Waiting.AddDefaulted();
		Waiting[WaitingIndex].Operative = Operative;
	}
	// Hold him where he stands: his own order stops, the formation leaves him alone until the span is free.
	Operative->SetTrafficHold(this, true);
	if (Operative->GetVelocity().SizeSquared2D() > 25.f || Operative->IsMoving())
	{
		Operative->StopOperative();
	}
	FWaiting& Entry = Waiting[WaitingIndex];
	if (!BridgeRules::ShouldRepeatRefusal(Entry.LastRefusalTime, Now, RefusalCooldownSeconds))
	{
		return;
	}
	Entry.LastRefusalTime = Now;
	++RefusalCount;
	UE_LOG(LogCodexTactics, Display, TEXT("Bridge %s: %s refused at the edge (%d on the span, capacity %d)"), *GetName(),
		*Operative->DisplayName.ToString(), Occupants.Num(), Capacity);
	if (UGameMessageSubsystem* Messages = GetWorld() ? GetWorld()->GetSubsystem<UGameMessageSubsystem>() : nullptr; Messages && !RefusalLine.IsEmpty())
	{
		Messages->PostMessage(Operative->DisplayName, RefusalLine);
	}
	OnBridgeCreak(Operative);
	GameplayEvents::Raise(this, GameplayEvents::BridgeRefused, Operative);
}

void ABridgeCrossingVolume::Release(AOperativeCharacter* Operative, bool bLeftSpan)
{
	const int32 Index = Occupants.IndexOfByPredicate([Operative](const FOccupant& Entry) { return Entry.Operative.Get() == Operative; });
	if (Index == INDEX_NONE)
	{
		return;
	}
	const float Entry = Occupants[Index].EntryCoordinate;
	Occupants.RemoveAt(Index);
	if (!Operative)
	{
		return;
	}
	Operative->SetTrafficHold(this, false);
	const float Exit = BridgeRules::SpanCoordinate(GetActorTransform(), Operative->GetActorLocation());
	if (bLeftSpan && BridgeRules::DidCross(Entry, Exit))
	{
		++CrossingCount;
		UE_LOG(LogCodexTactics, Display, TEXT("Bridge %s: %s crossed (%.0f -> %.0f cm)"), *GetName(), *Operative->DisplayName.ToString(), Entry, Exit);
		GameplayEvents::Raise(this, GameplayEvents::BridgeCrossed, Operative);
	}
	else
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Bridge %s: %s stepped back off the span"), *GetName(), *Operative->DisplayName.ToString());
	}
}

#undef LOCTEXT_NAMESPACE
