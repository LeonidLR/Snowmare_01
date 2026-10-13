#include "Level/CrawlGapActor.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Components/BoxComponent.h"
#include "Core/PlayerCommands.h"
#include "Engine/World.h"
#include "Level/LevelObstacleRules.h"
#include "UI/GameMessageSubsystem.h"
#include "Vehicles/VehicleNavigation.h"

#define LOCTEXT_NAMESPACE "CrawlGapActor"

UNavArea_CrawlOnly::UNavArea_CrawlOnly()
{
	DefaultCost = 1.f; // no path penalty: a prone body goes straight under when that is the shorter way (the crawl itself is slow)
	DrawColor = FColor(255, 120, 200);
}

UNavFilter_NoCrawl::UNavFilter_NoCrawl()
{
	FNavigationFilterArea& Area = Areas.AddDefaulted_GetRef();
	Area.AreaClass = UNavArea_CrawlOnly::StaticClass();
	Area.bIsExcluded = true;
}

ACrawlGapActor::ACrawlGapActor()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false; // ticks only while somebody is in the approach zone
	GapRoot = CreateDefaultSubobject<USceneComponent>(TEXT("GapRoot"));
	SetRootComponent(GapRoot);

	Bar = CreateDefaultSubobject<UBoxComponent>(TEXT("Bar"));
	Bar->SetupAttachment(GapRoot);
	Bar->SetCollisionProfileName(TEXT("BlockAllDynamic"));
	Bar->SetGenerateOverlapEvents(false);
	Bar->SetCanEverAffectNavigation(false); // the navmesh stays under the fence: the crawl-only area rules who may use it
	Bar->ShapeColor = FColor(255, 120, 200);
	Bar->SetHiddenInGame(true);

	Approach = CreateDefaultSubobject<UBoxComponent>(TEXT("Approach"));
	Approach->SetupAttachment(GapRoot);
	Approach->SetCollisionProfileName(TEXT("OverlapAllDynamic"));
	Approach->SetGenerateOverlapEvents(true);
	Approach->SetCanEverAffectNavigation(false);
	Approach->ShapeColor = FColor(255, 180, 220);
	Approach->SetHiddenInGame(true);

	NavArea = CreateDefaultSubobject<UVehicleNavObstacleComponent>(TEXT("NavArea"));
	NavArea->AreaClass = UNavArea_CrawlOnly::StaticClass();
	NavArea->MarginCm = 0.f;

	RefusalLine = LOCTEXT("Refusal", "Too low - go prone and crawl under.");
	ClearanceCm = CrawlGapRules::DefaultClearanceCm;
	BarTopCm = CrawlGapRules::DefaultBarTopCm;
	ApproachCm = CrawlGapRules::DefaultApproachCm;
	ApplyShape();
}

void ACrawlGapActor::ApplyShape()
{
	const float BarTop = FMath::Max(BarTopCm, ClearanceCm + 10.f);
	const float HalfWidth = GapWidthCm * 0.5f;
	Bar->SetBoxExtent(FVector(DepthCm * 0.5f, HalfWidth, (BarTop - ClearanceCm) * 0.5f));
	Bar->SetRelativeLocation(FVector(0.f, 0.f, (BarTop + ClearanceCm) * 0.5f));
	Approach->SetBoxExtent(FVector(DepthCm * 0.5f + ApproachCm, HalfWidth, 120.f));
	Approach->SetRelativeLocation(FVector(0.f, 0.f, 120.f));
	// The crawl-only area: the fence line plus one agent radius each side, so no standing path squeezes past the bar's edge; tall
	// enough (+-1.5 m) to reach the floor polys whether the actor sits on the ground or a little above it.
	NavArea->SetLocalBox(FVector(0.f, 0.f, 0.f), FVector(DepthCm * 0.5f + 40.f, HalfWidth, 150.f));
}

void ACrawlGapActor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	ApplyShape();
}

void ACrawlGapActor::BeginPlay()
{
	Super::BeginPlay();
	ApplyShape();
	NavArea->SetObstacleActive(true);
	Approach->OnComponentBeginOverlap.AddDynamic(this, &ACrawlGapActor::HandleBeginOverlap);
}

bool ACrawlGapActor::IsSquadOperative(const AActor* Actor) const
{
	const AOperativeCharacter* Operative = Cast<AOperativeCharacter>(Actor);
	if (!Operative)
	{
		return false;
	}
	const USquadSubsystem* Squad = GetWorld() ? GetWorld()->GetSubsystem<USquadSubsystem>() : nullptr;
	return Squad && Squad->GetMembers().Contains(Operative);
}

void ACrawlGapActor::HandleBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp,
	int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	if (IsSquadOperative(OtherActor))
	{
		SetActorTickEnabled(true);
		UpdateTraffic();
	}
}

void ACrawlGapActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	UpdateTraffic();
	if (Tracked.IsEmpty())
	{
		SetActorTickEnabled(false);
	}
}

void ACrawlGapActor::SetHeld(FTracked& Entry, AOperativeCharacter& Operative, bool bHeld)
{
	if (Entry.bHeld != bHeld)
	{
		Entry.bHeld = bHeld;
		Operative.SetTrafficHold(this, bHeld);
	}
}

void ACrawlGapActor::UpdateTraffic()
{
	TArray<AActor*> Inside;
	Approach->GetOverlappingActors(Inside, AOperativeCharacter::StaticClass());
	const FTransform ToWorld = GetActorTransform();
	// Left the zone: a pass when he came out on the other side (prone crawl), otherwise just gone.
	for (int32 Index = Tracked.Num() - 1; Index >= 0; --Index)
	{
		AOperativeCharacter* Operative = Tracked[Index].Operative.Get();
		if (Operative && Inside.Contains(Operative))
		{
			continue;
		}
		if (Operative)
		{
			SetHeld(Tracked[Index], *Operative, false);
			const int32 ExitSide = CrawlGapRules::Side(static_cast<float>(ToWorld.InverseTransformPosition(Operative->GetActorLocation()).X));
			UE_LOG(LogCodexTactics, Verbose, TEXT("CrawlGap %s: %s left the approach zone (entry side %d, exit side %d)"), *GetName(),
				*Operative->DisplayName.ToString(), Tracked[Index].EntrySide, ExitSide);
			if (CrawlGapRules::DidPass(Tracked[Index].EntrySide, ExitSide))
			{
				++PassCount;
				UE_LOG(LogCodexTactics, Display, TEXT("CrawlGap %s: %s crawled through"), *GetName(), *Operative->DisplayName.ToString());
				GameplayEvents::Raise(this, GameplayEvents::CrawlGapPassed, Operative);
			}
		}
		Tracked.RemoveAt(Index);
	}
	for (AActor* Actor : Inside)
	{
		if (!IsSquadOperative(Actor))
		{
			continue;
		}
		AOperativeCharacter* Operative = static_cast<AOperativeCharacter*>(Actor);
		const FVector Local = ToWorld.InverseTransformPosition(Operative->GetActorLocation());
		int32 Index = Tracked.IndexOfByPredicate([Operative](const FTracked& Entry) { return Entry.Operative.Get() == Operative; });
		if (Index == INDEX_NONE)
		{
			Index = Tracked.AddDefaulted();
			Tracked[Index].Operative = Operative;
			Tracked[Index].EntrySide = CrawlGapRules::Side(static_cast<float>(Local.X));
		}
		FTracked& Entry = Tracked[Index];
		if (CrawlGapRules::CanPassUnder(Operative->GetStance()))
		{
			SetHeld(Entry, *Operative, false); // prone: the crawl is his
			continue;
		}
		// Standing / crouched: stopped when he walks at the fence; a body that merely stands in the zone (his path ended at the
		// closed strip) gets the hint once and may walk away freely.
		const FVector ToFence = ToWorld.TransformVectorNoScale(FVector(-Local.X, 0.f, 0.f));
		if (CrawlGapRules::IsApproaching(ToFence, Operative->GetVelocity()))
		{
			Refuse(Entry, *Operative, /*bStop*/ true);
		}
		else if (!Entry.bHinted && !Operative->IsMoving())
		{
			Refuse(Entry, *Operative, /*bStop*/ false);
		}
	}
}

void ACrawlGapActor::Refuse(FTracked& Entry, AOperativeCharacter& Operative, bool bStop)
{
	Entry.bHinted = true;
	if (bStop)
	{
		Operative.StopOperative();
		SetHeld(Entry, Operative, true); // the formation leaves him here until he goes prone or walks off
	}
	const float Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
	if (!BridgeRules::ShouldRepeatRefusal(Entry.LastRefusalTime, Now, RefusalCooldownSeconds))
	{
		return;
	}
	Entry.LastRefusalTime = Now;
	++RefusalCount;
	UE_LOG(LogCodexTactics, Display, TEXT("CrawlGap %s: %s stopped (%s): too low"), *GetName(), *Operative.DisplayName.ToString(),
		*AOperativeCharacter::GetStanceDisplayName(Operative.GetStance()).ToString());
	if (UGameMessageSubsystem* Messages = GetWorld() ? GetWorld()->GetSubsystem<UGameMessageSubsystem>() : nullptr; Messages && !RefusalLine.IsEmpty())
	{
		Messages->PostMessage(Operative.DisplayName, RefusalLine);
	}
	GameplayEvents::Raise(this, GameplayEvents::CrawlGapRefused, &Operative);
}

#undef LOCTEXT_NAMESPACE
