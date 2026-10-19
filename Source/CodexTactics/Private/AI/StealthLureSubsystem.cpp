#include "AI/StealthLureSubsystem.h"

#include "AI/LureStoneActor.h"
#include "AI/WorldAIPauseSubsystem.h"
#include "Characters/EnemyCharacter.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/EnemyRegistrySubsystem.h"
#include "Combat/GrenadeRules.h"
#include "Combat/GrenadeSubsystem.h"
#include "Combat/HealthComponent.h"
#include "Core/PlayerCommands.h"
#include "Data/EnemyPerception.h"
#include "Dom/JsonObject.h"
#include "Engine/World.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "UI/FloatingTextSubsystem.h"
#include "UI/GameMessageSubsystem.h"
#include "Vehicles/VehicleSubsystem.h"

#define LOCTEXT_NAMESPACE "StealthLureSubsystem"

namespace StealthLureInternal
{
	/** The lure noise is heard from this height over its ground point (a whistle at the mouth, a stone's clatter). */
	constexpr float NoiseHeightCm = 60.f;
	/** A preview fades when not refreshed for this long (real seconds). */
	constexpr double PreviewHoldSeconds = 0.2;
	/** The preview's listeners are re-traced at most this often, or when the point moved more than PreviewMoveCm. */
	constexpr double PreviewTraceSeconds = 0.1;
	constexpr float PreviewMoveCm = 50.f;

	const FName WhistleEvent(TEXT("StealthLure.Whistle"));
	const FName ThrowEvent(TEXT("StealthLure.Throw"));
}

bool UStealthLureSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId UStealthLureSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UStealthLureSubsystem, STATGROUP_Tickables);
}

FStealthLureParams UStealthLureSubsystem::GetParams()
{
	return EnemyPerception::GetLure();
}

double UStealthLureSubsystem::Now() const
{
	const UWorld* World = GetWorld();
	return World ? World->GetTimeSeconds() : 0.0;
}

void UStealthLureSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (bRingShown && !IsPreviewActive())
	{
		HideRing();
	}
}

float UStealthLureSubsystem::GetCooldownLeft(const AOperativeCharacter* Operative, EStealthLureKind Kind) const
{
	const FLureCooldowns* Found = Operative ? Cooldowns.Find(Operative) : nullptr;
	if (!Found)
	{
		return 0.f;
	}
	return StealthLureRules::CooldownLeft(Kind == EStealthLureKind::Whistle ? Found->WhistleReadyAt : Found->ThrowReadyAt, Now());
}

bool UStealthLureSubsystem::IsSquadDetected() const
{
	for (const AEnemyCharacter* Enemy : UEnemyRegistrySubsystem::GetEnemyCharacters(GetWorld()))
	{
		if (Enemy && Enemy->IsHuntingSquad())
		{
			return true;
		}
	}
	return false;
}

EStealthLureRefusal UStealthLureSubsystem::GetAvailability(const AOperativeCharacter* Operative, EStealthLureKind Kind) const
{
	FStealthLureGate Gate;
	Gate.bOperativeAble = Operative && Operative->HealthComponent && Operative->HealthComponent->IsAlive() && !Operative->IsKnockedDown()
		&& !VehicleQueries::IsMounted(Operative);
	if (const UGameFlowSubsystem* Flow = GetWorld() ? GetWorld()->GetSubsystem<UGameFlowSubsystem>() : nullptr)
	{
		Gate.Phase = Flow->GetPhase();
		Gate.CombatMode = Flow->GetCombatMode();
	}
	// The world scan only when the cheaper gates pass (the action bar asks every frame).
	if (Gate.bOperativeAble && StealthLureRules::IsExploring(Gate.Phase, Gate.CombatMode))
	{
		Gate.bSquadDetected = IsSquadDetected();
	}
	Gate.CooldownLeftSeconds = GetCooldownLeft(Operative, Kind);
	return StealthLureRules::CheckAvailability(Gate);
}

void UStealthLureSubsystem::StartCooldown(const AOperativeCharacter* Operative, EStealthLureKind Kind)
{
	if (!Operative)
	{
		return;
	}
	FLureCooldowns& Entry = Cooldowns.FindOrAdd(Operative);
	const double ReadyAt = Now() + StealthLureRules::GetCooldown(GetParams(), Kind);
	(Kind == EStealthLureKind::Whistle ? Entry.WhistleReadyAt : Entry.ThrowReadyAt) = ReadyAt;
}

void UStealthLureSubsystem::PostRefusal(const AOperativeCharacter* Operative, EStealthLureKind Kind, EStealthLureRefusal Refusal) const
{
	if (UGameMessageSubsystem* Messages = GetWorld() ? GetWorld()->GetSubsystem<UGameMessageSubsystem>() : nullptr)
	{
		Messages->PostMessage(Operative ? Operative->DisplayName : LOCTEXT("Squad", "SQUAD"),
			StealthLureRules::GetRefusalText(Kind, Refusal, GetCooldownLeft(Operative, Kind)));
	}
}

bool UStealthLureSubsystem::Whistle(AOperativeCharacter* Operative)
{
	const EStealthLureRefusal Refusal = GetAvailability(Operative, EStealthLureKind::Whistle);
	if (Refusal != EStealthLureRefusal::None)
	{
		PostRefusal(Operative, EStealthLureKind::Whistle, Refusal);
		UE_LOG(LogCodexTactics, Display, TEXT("[Stealth] whistle refused (%s)"), *UEnum::GetValueAsString(Refusal));
		return false;
	}
	const FVector Feet = Operative->GetActorLocation() - FVector(0.f, 0.f, Operative->GetSimpleCollisionHalfHeight());
	StartCooldown(Operative, EStealthLureKind::Whistle);
	const int32 Heard = EmitNoise(Feet, EStealthLureKind::Whistle, Operative);
	if (UGameMessageSubsystem* Messages = GetWorld()->GetSubsystem<UGameMessageSubsystem>())
	{
		Messages->PostMessage(Operative->DisplayName, StealthLureRules::GetFeedLine(EStealthLureKind::Whistle));
	}
	UFloatingTextSubsystem::SpawnAboveOperative(Operative, TEXT("~ whistle ~"), FLinearColor(0.75f, 0.9f, 1.f));
	FGameplayEventContext Context;
	Context.Subject = Operative;
	Context.Kind = TEXT("Whistle");
	Context.Location = Feet;
	GameplayEvents::RaiseWithContext(this, StealthLureInternal::WhistleEvent, Context);
	UE_LOG(LogCodexTactics, Display, TEXT("[Stealth] %s whistles at %s: %d enemies heard it"), *Operative->DisplayName.ToString(), *Feet.ToCompactString(), Heard);
	return true;
}

ALureStoneActor* UStealthLureSubsystem::ThrowAt(AOperativeCharacter* Operative, const FVector& Target)
{
	const EStealthLureRefusal Refusal = GetAvailability(Operative, EStealthLureKind::Throw);
	if (Refusal != EStealthLureRefusal::None)
	{
		PostRefusal(Operative, EStealthLureKind::Throw, Refusal);
		UE_LOG(LogCodexTactics, Display, TEXT("[Stealth] throw refused (%s)"), *UEnum::GetValueAsString(Refusal));
		return nullptr;
	}
	const FVector Landing = GrenadeRules::ClampTarget(Operative->GetActorLocation(), Target,
		StealthLureRules::GetThrowRange(GetParams(), Operative->GetStance()));
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ALureStoneActor* Stone = GetWorld()->SpawnActor<ALureStoneActor>(StoneClass ? *StoneClass : ALureStoneActor::StaticClass(),
		Operative->GetActorLocation() + FVector(0.f, 0.f, GrenadeRules::HandHeight), FRotator::ZeroRotator, Params);
	if (!Stone)
	{
		return nullptr;
	}
	// The grenade throw clip (the same hand movement); the stone leaves the hand at its release point.
	Operative->GrenadeReleaseSeconds = -1.f;
	Operative->OnGrenadeThrowNative.Broadcast();
	Operative->ReceiveGrenadeThrow();
	Stone->Throw(Operative, Landing, GrenadeRules::ReleaseDelay(Operative->GrenadeThrowDuration, Operative->GrenadeReleaseSeconds));
	StartCooldown(Operative, EStealthLureKind::Throw);
	if (UGameMessageSubsystem* Messages = GetWorld()->GetSubsystem<UGameMessageSubsystem>())
	{
		Messages->PostMessage(Operative->DisplayName, StealthLureRules::GetFeedLine(EStealthLureKind::Throw));
	}
	FGameplayEventContext Context;
	Context.Subject = Operative;
	Context.Kind = TEXT("Throw");
	Context.Location = Landing;
	GameplayEvents::RaiseWithContext(this, StealthLureInternal::ThrowEvent, Context);
	UE_LOG(LogCodexTactics, Display, TEXT("[Stealth] %s throws a stone %.1f m to %s"), *Operative->DisplayName.ToString(),
		FVector::Dist2D(Operative->GetActorLocation(), Landing) / 100.f, *Landing.ToCompactString());
	return Stone;
}

TArray<AEnemyCharacter*> UStealthLureSubsystem::FindListeners(const FVector& Location, EStealthLureKind Kind) const
{
	TArray<AEnemyCharacter*> Out;
	const UWorld* World = GetWorld();
	if (!World)
	{
		return Out;
	}
	const FStealthLureParams Params = GetParams();
	const float Radius = StealthLureRules::GetRadius(Params, Kind);
	const float SearchMultiplier = EnemyPerception::GetSearch().PerceptionMultiplier;
	const FVector NoisePoint = Location + FVector(0.f, 0.f, StealthLureInternal::NoiseHeightCm);
	for (AEnemyCharacter* Enemy : UEnemyRegistrySubsystem::GetEnemyCharacters(World))
	{
		if (!Enemy)
		{
			continue;
		}
		FStealthLureListener Listener;
		Listener.bAlive = !Enemy->IsDying() && Enemy->GetHealthComponent() && Enemy->GetHealthComponent()->IsAlive();
		Listener.bOnPatrol = Enemy->IsOnPatrol();
		Listener.bTurnBasedHeld = Enemy->IsTurnBasedHeld();
		Listener.bSearching = Enemy->IsSearching();
		Listener.DistanceCm = FVector::Dist(Enemy->GetActorLocation(), NoisePoint);
		Listener.OcclusionPerWall = Enemy->GetPerception().HearingOcclusionPerWall;
		// Walls are traced only when the open-air radius reaches it (the footstep rule).
		if (!StealthLureRules::Hears(Listener, Radius, SearchMultiplier))
		{
			continue;
		}
		Listener.Walls = Enemy->CountHearingWallsTo(NoisePoint);
		if (!StealthLureRules::Hears(Listener, Radius, SearchMultiplier))
		{
			// Muffled on the straight line: the noise may still come round the wall / through a doorway (its walking path in the open).
			Listener.PathCm = Enemy->GetHearingPathLengthTo(NoisePoint);
		}
		if (StealthLureRules::Hears(Listener, Radius, SearchMultiplier))
		{
			Out.Add(Enemy);
		}
	}
	return Out;
}

int32 UStealthLureSubsystem::EmitNoise(const FVector& Location, EStealthLureKind Kind, const AActor* Source)
{
	LastListeners.Reset();
	UWorld* World = GetWorld();
	if (!World || UWorldAIPauseSubsystem::IsPausedIn(World))
	{
		return 0;
	}
	const FStealthLureParams Params = GetParams();
	FPatrolSearchSpec Spec;
	Spec.bLure = true;
	Spec.DurationSeconds = Params.InvestigateSeconds;
	Spec.SweepRadiusCm = Params.InvestigateSweepRadiusCm;
	Spec.ApproachSpeedFraction = Params.ApproachSpeedFraction;
	Spec.ListenSeconds = Params.ListenSeconds;
	// EVERY enemy that hears it comes (user correction 2026-10-18: a horde in range = all of them).
	const TArray<AEnemyCharacter*> Listeners = FindListeners(Location, Kind);
	// Who was in open-air reach but did not hear it (walls): one line each, the first thing to read when "nobody came".
	const float OpenRadius = StealthLureRules::GetRadius(Params, Kind);
	const float SearchMultiplier = EnemyPerception::GetSearch().PerceptionMultiplier;
	const FVector NoisePoint = Location + FVector(0.f, 0.f, StealthLureInternal::NoiseHeightCm);
	for (AEnemyCharacter* Enemy : UEnemyRegistrySubsystem::GetEnemyCharacters(World))
	{
		if (!Enemy || Listeners.Contains(Enemy) || !Enemy->IsOnPatrol() || Enemy->IsDying() || Enemy->IsTurnBasedHeld())
		{
			continue;
		}
		FStealthLureListener Listener;
		Listener.bAlive = true;
		Listener.bOnPatrol = true;
		Listener.bSearching = Enemy->IsSearching();
		Listener.DistanceCm = FVector::Dist(Enemy->GetActorLocation(), NoisePoint);
		Listener.OcclusionPerWall = Enemy->GetPerception().HearingOcclusionPerWall;
		if (Listener.DistanceCm > StealthLureRules::OpenRadius(Listener, OpenRadius, SearchMultiplier))
		{
			continue; // out of reach even in the open
		}
		Listener.Walls = Enemy->CountHearingWallsTo(NoisePoint);
		Listener.PathCm = Enemy->GetHearingPathLengthTo(NoisePoint);
		if (StealthLureRules::Hears(Listener, OpenRadius, SearchMultiplier))
		{
			continue; // defensive: FindListeners has it
		}
		UE_LOG(LogCodexTactics, Display, TEXT("[Stealth] %s does not hear the %s: %.1f m, %d wall(s), radius %.1f m, walking path %.1f m%s"), *Enemy->GetName(),
			Kind == EStealthLureKind::Whistle ? TEXT("whistle") : TEXT("stone"), Listener.DistanceCm / 100.f, Listener.Walls,
			StealthLureRules::EffectiveRadius(Listener, OpenRadius, SearchMultiplier) / 100.f, Listener.PathCm / 100.f,
			Listener.bSearching ? TEXT(" (searching)") : TEXT(""));
	}
	// A NEWER noise re-targets an enemy that already investigates (a stone, a trap, a scent; fix 2026-10-18) - see ReactToNoise.
	for (AEnemyCharacter* Enemy : Listeners)
	{
		if (IsValid(Enemy) && Enemy->IsOnPatrol())
		{
			const ENoiseReaction Reaction = Enemy->StartPatrolSearchWith(Location, Spec);
			if (Reaction != ENoiseReaction::KeepScent && Reaction != ENoiseReaction::Ignored)
			{
				LastListeners.Add(Enemy);
				// The lure worked on it (the Demo 01 A4 lure lesson completes on it, 2026-10-19).
				FGameplayEventContext Context;
				Context.Subject = Enemy;
				Context.Target = const_cast<AActor*>(Source);
				Context.Kind = Kind == EStealthLureKind::Whistle ? FName(TEXT("Whistle")) : FName(TEXT("Throw"));
				Context.Location = Location;
				GameplayEvents::RaiseWithContext(this, GameplayEvents::LureHeard, Context);
			}
		}
	}
	UE_LOG(LogCodexTactics, Display, TEXT("[Stealth] lure %s at %s (radius %.0f m, by %s): %d enemies investigate"),
		Kind == EStealthLureKind::Whistle ? TEXT("whistle") : TEXT("stone"), *Location.ToCompactString(),
		StealthLureRules::GetRadius(Params, Kind) / 100.f, Source ? *Source->GetName() : TEXT("-"), LastListeners.Num());
	return LastListeners.Num();
}

void UStealthLureSubsystem::UpdatePreview(const FVector& Location, EStealthLureKind Kind, bool bDrawRing)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	const double Time = World->GetRealTimeSeconds();
	if (Time - PreviewTraceStamp >= StealthLureInternal::PreviewTraceSeconds || Kind != PreviewKind
		|| FVector::Dist(Location, PreviewLocation) > StealthLureInternal::PreviewMoveCm || !IsPreviewActive())
	{
		PreviewListeners.Reset();
		for (AEnemyCharacter* Enemy : FindListeners(Location, Kind))
		{
			PreviewListeners.Add(Enemy);
		}
		PreviewTraceStamp = Time;
	}
	PreviewLocation = Location;
	PreviewKind = Kind;
	PreviewStamp = Time;
	if (!bDrawRing)
	{
		return;
	}
	if (!RingActor)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		RingActor = World->SpawnActor<AGrenadeAimActor>(FVector::ZeroVector, FRotator::ZeroRotator, Params);
	}
	if (RingActor)
	{
		RingActor->SetLureStyle(true);
		RingActor->ShowRing(Location, StealthLureRules::GetRadius(GetParams(), Kind));
		RingActor->SetActorHiddenInGame(false);
		bRingShown = true;
	}
}

bool UStealthLureSubsystem::IsPreviewActive() const
{
	const UWorld* World = GetWorld();
	return World && World->GetRealTimeSeconds() - PreviewStamp <= StealthLureInternal::PreviewHoldSeconds;
}

TArray<AEnemyCharacter*> UStealthLureSubsystem::GetPreviewListeners() const
{
	TArray<AEnemyCharacter*> Out;
	if (!IsPreviewActive())
	{
		return Out;
	}
	for (const TWeakObjectPtr<AEnemyCharacter>& Enemy : PreviewListeners)
	{
		if (Enemy.IsValid())
		{
			Out.Add(Enemy.Get());
		}
	}
	return Out;
}

void UStealthLureSubsystem::HideRing()
{
	if (RingActor)
	{
		RingActor->SetActorHiddenInGame(true);
	}
	bRingShown = false;
}

TSharedRef<FJsonObject> UStealthLureSubsystem::CaptureSave() const
{
	TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
	for (const TPair<TWeakObjectPtr<const AOperativeCharacter>, FLureCooldowns>& Entry : Cooldowns)
	{
		const AOperativeCharacter* Operative = Entry.Key.Get();
		if (!Operative)
		{
			continue;
		}
		const float Whistle = StealthLureRules::CooldownLeft(Entry.Value.WhistleReadyAt, Now());
		const float Throw = StealthLureRules::CooldownLeft(Entry.Value.ThrowReadyAt, Now());
		if (Whistle <= 0.f && Throw <= 0.f)
		{
			continue;
		}
		TSharedRef<FJsonObject> Item = MakeShared<FJsonObject>();
		Item->SetNumberField(TEXT("whistle_s"), Whistle);
		Item->SetNumberField(TEXT("throw_s"), Throw);
		Json->SetObjectField(Operative->DisplayName.ToString(), Item);
	}
	return Json;
}

void UStealthLureSubsystem::RestoreSave(const FJsonObject& Json)
{
	Cooldowns.Reset();
	const USquadSubsystem* Squad = GetWorld() ? GetWorld()->GetSubsystem<USquadSubsystem>() : nullptr;
	for (const AOperativeCharacter* Member : Squad ? Squad->GetMembers() : TArray<AOperativeCharacter*>())
	{
		const TSharedPtr<FJsonObject>* Item = nullptr;
		if (!Member || !Json.TryGetObjectField(Member->DisplayName.ToString(), Item) || !Item || !Item->IsValid())
		{
			continue;
		}
		double Whistle = 0.0;
		double Throw = 0.0;
		(*Item)->TryGetNumberField(TEXT("whistle_s"), Whistle);
		(*Item)->TryGetNumberField(TEXT("throw_s"), Throw);
		FLureCooldowns& Entry = Cooldowns.FindOrAdd(Member);
		Entry.WhistleReadyAt = Now() + FMath::Max(Whistle, 0.0);
		Entry.ThrowReadyAt = Now() + FMath::Max(Throw, 0.0);
	}
}

#undef LOCTEXT_NAMESPACE
