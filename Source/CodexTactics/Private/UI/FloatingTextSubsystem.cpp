#include "UI/FloatingTextSubsystem.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"

void UFloatingTextSubsystem::Spawn(const FVector& WorldLocation, const FString& Text, const FLinearColor& Color, float Duration, float Rise)
{
	FCombatFloatingText& Entry = Texts.AddDefaulted_GetRef();
	Entry.Start = WorldLocation;
	Entry.Text = Text;
	Entry.Color = Color;
	Entry.StartTime = GetWorld()->GetTimeSeconds();
	Entry.Duration = Duration;
	Entry.Rise = Rise;
	History.Add(Text);
	if (History.Num() > 200)
	{
		History.RemoveAt(0);
	}
}

FVector UFloatingTextSubsystem::FeetOf(const AActor* Actor)
{
	const ACharacter* Character = Cast<ACharacter>(Actor);
	const float HalfHeight = Character && Character->GetCapsuleComponent() ? Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : 0.f;
	return Actor->GetActorLocation() - FVector(0.f, 0.f, HalfHeight);
}

void UFloatingTextSubsystem::SpawnAboveOperative(const AActor* Operative, const FString& Text, const FLinearColor& Color)
{
	UFloatingTextSubsystem* Self = Operative && Operative->GetWorld() ? Operative->GetWorld()->GetSubsystem<UFloatingTextSubsystem>() : nullptr;
	if (Self)
	{
		Self->Spawn(FeetOf(Operative) + FVector(0.f, 0.f, 200.f), Text, Color, Text.Len() > 15 ? 1.8f : 0.75f, 120.f);
	}
}

void UFloatingTextSubsystem::SpawnAboveEnemy(const AActor* Enemy, const FString& Text, const FLinearColor& Color)
{
	UFloatingTextSubsystem* Self = Enemy && Enemy->GetWorld() ? Enemy->GetWorld()->GetSubsystem<UFloatingTextSubsystem>() : nullptr;
	if (Self)
	{
		const FVector Jitter(FMath::FRandRange(-30.f, 30.f), FMath::FRandRange(-30.f, 30.f), 250.f);
		Self->Spawn(FeetOf(Enemy) + Jitter, Text, Color, 0.7f, 120.f);
	}
}

void UFloatingTextSubsystem::SpawnAboveMine(const AActor* Mine, const FString& Text, const FLinearColor& Color)
{
	UFloatingTextSubsystem* Self = Mine && Mine->GetWorld() ? Mine->GetWorld()->GetSubsystem<UFloatingTextSubsystem>() : nullptr;
	if (Self)
	{
		Self->Spawn(Mine->GetActorLocation() + FVector(0.f, 0.f, 80.f), Text, Color, Text.Len() > 15 ? 1.8f : 0.8f, 80.f);
	}
}

const TArray<FCombatFloatingText>& UFloatingTextSubsystem::GetTexts()
{
	const double Now = GetWorld()->GetTimeSeconds();
	Texts.RemoveAll([Now](const FCombatFloatingText& Entry) { return Entry.GetProgress(Now) >= 1.f; });
	return Texts;
}

bool UFloatingTextSubsystem::HasShown(const FString& Part) const
{
	return History.ContainsByPredicate([&Part](const FString& Text) { return Text.Contains(Part); });
}
