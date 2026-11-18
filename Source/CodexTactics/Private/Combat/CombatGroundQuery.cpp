#include "Combat/CombatGroundQuery.h"

#include "Characters/EnemyAIRules.h"
#include "Combat/FireGateRules.h"
#include "Combat/SquadFireRules.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "UObject/ObjectKey.h"

namespace CombatGround
{
	FVector FeetOf(const AActor& Actor)
	{
		const ACharacter* Character = Cast<ACharacter>(&Actor);
		const UCapsuleComponent* Capsule = Character ? Character->GetCapsuleComponent() : nullptr;
		return Capsule ? Actor.GetActorLocation() - FVector(0.f, 0.f, Capsule->GetScaledCapsuleHalfHeight()) : Actor.GetActorLocation();
	}

	namespace
	{
		/** Floor under XY between TopZ and BottomZ on world geometry (no pawns: a body is no floor). */
		bool TraceFloor(const UWorld& World, const FVector2D& XY, float TopZ, float BottomZ, const FCollisionQueryParams& Params, float& OutZ)
		{
			FCollisionObjectQueryParams Objects;
			Objects.AddObjectTypesToQuery(ECC_WorldStatic);
			Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
			FHitResult Hit;
			if (World.LineTraceSingleByObjectType(Hit, FVector(XY, TopZ), FVector(XY, BottomZ), Objects, Params))
			{
				OutZ = Hit.ImpactPoint.Z;
				return true;
			}
			return false;
		}

		FCollisionQueryParams ProbeParams(TConstArrayView<const AActor*> Ignore)
		{
			FCollisionQueryParams Params(SCENE_QUERY_STAT(CombatGroundProbe), false);
			for (const AActor* Actor : Ignore)
			{
				if (Actor)
				{
					Params.AddIgnoredActor(Actor);
				}
			}
			return Params;
		}

		const FFireGateConfig& Config()
		{
			return FireGateRules::GetConfig();
		}
	}

	TArray<FFloorSample> ProbeFloor(const UWorld& World, const FVector& FromFeet, const FVector& ToFeet, TConstArrayView<const AActor*> Ignore,
		float& OutSpacingCm)
	{
		const FGroundProbeConfig& Probe = Config().GroundProbe;
		const float StepMax = Config().MeleeReach.StepMaxCm;
		const float Horizontal = FVector::Dist2D(FromFeet, ToFeet);
		const FVector2D From(FromFeet);
		const FVector2D Toward = Horizontal > KINDA_SMALL_NUMBER ? (FVector2D(ToFeet) - From) / Horizontal : FVector2D::ZeroVector;
		const FVector2D Side(-Toward.Y, Toward.X);
		const float TopZ = FMath::Max(FromFeet.Z, ToFeet.Z) + Probe.UpCm;
		const float BottomZ = FMath::Min(FromFeet.Z, ToFeet.Z) - Probe.DownCm;
		const FCollisionQueryParams Params = ProbeParams(Ignore);

		TArray<FFloorSample> Profile;
		Profile.Add({ 0.f, static_cast<float>(FromFeet.Z), true });
		for (const float Distance : CombatGroundRules::ProbeDistances(Horizontal, Probe, OutSpacingCm))
		{
			const FFloorSample& Previous = Profile.Last();
			const FVector2D At = From + Toward * Distance;
			FFloorSample Sample{ Distance, 0.f, false };
			float Z = 0.f;
			if (TraceFloor(World, At, TopZ, BottomZ, Params, Z))
			{
				Sample.Z = Z;
				Sample.bHit = true;
			}
			// Missed, or off the previous floor (a narrow stair seen diagonally): the floor nearest the previous one to either side.
			if (Probe.LateralCm > 0.f && (!Sample.bHit || (Previous.bHit && FMath::Abs(Sample.Z - Previous.Z) > StepMax)))
			{
				for (const float Sign : { -1.f, 1.f })
				{
					if (TraceFloor(World, At + Side * (Sign * Probe.LateralCm), TopZ, BottomZ, Params, Z)
						&& (!Sample.bHit || (Previous.bHit && FMath::Abs(Z - Previous.Z) < FMath::Abs(Sample.Z - Previous.Z))))
					{
						Sample.Z = Z;
						Sample.bHit = true;
					}
				}
			}
			Profile.Add(Sample);
		}
		Profile.Add({ Horizontal, static_cast<float>(ToFeet.Z), true });
		return Profile;
	}

	bool CanMeleeAt(const UWorld& World, const FVector& AttackerFeet, const FVector& TargetFeet, float AttackRangeCm, TConstArrayView<const AActor*> Ignore)
	{
		const FMeleeReachConfig& Melee = Config().MeleeReach;
		const bool bOffGround = EnemyAIRules::CanMeleeOnGround(AttackerFeet, TargetFeet, AttackRangeCm, false, Melee);
		const bool bOnGround = EnemyAIRules::CanMeleeOnGround(AttackerFeet, TargetFeet, AttackRangeCm, true, Melee);
		if (bOffGround == bOnGround)
		{
			return bOnGround; // the floor between them changes nothing: no probe
		}
		float Spacing = 0.f;
		const TArray<FFloorSample> Profile = ProbeFloor(World, AttackerFeet, TargetFeet, Ignore, Spacing);
		return EnemyAIRules::CanMeleeOverProfile(AttackerFeet, TargetFeet, AttackRangeCm, Profile, Melee);
	}

	namespace
	{
		/** One cached shared-ground verdict (final review 3+4): ground_probe cache_s per (attacker, target). */
		struct FSharedGroundEntry
		{
			TObjectKey<UWorld> World;
			double Time = 0.0;
			bool bShared = false;
		};
		using FSharedGroundKey = TPair<TObjectKey<AActor>, TObjectKey<AActor>>;
		TMap<FSharedGroundKey, FSharedGroundEntry> SharedGroundCache; // game thread only (the AI / smoke ticks)

		/**
		 * The melee shared-ground verdict between two characters: one ProbeFloor per (attacker, target) per cache_s, shared by
		 * CanMelee and MeleeReachPlanar (the flank bite reach, the bite and the close-in step in one tick probe once).
		 */
		bool IsSharedGroundBetween(const UWorld& World, const AActor& Attacker, const AActor& Target, const FVector& AttackerFeet,
			const FVector& TargetFeet)
		{
			const float CacheS = Config().GroundProbe.CacheS;
			const double Now = World.GetTimeSeconds();
			const FSharedGroundKey Key(TObjectKey<AActor>(&Attacker), TObjectKey<AActor>(&Target));
			if (CacheS > 0.f)
			{
				if (const FSharedGroundEntry* Entry = SharedGroundCache.Find(Key);
					Entry && Entry->World == TObjectKey<UWorld>(&World) && Now >= Entry->Time && Now - Entry->Time < CacheS)
				{
					return Entry->bShared;
				}
			}
			float Spacing = 0.f;
			const AActor* Ignore[] = { &Attacker, &Target };
			const TArray<FFloorSample> Profile = ProbeFloor(World, AttackerFeet, TargetFeet, Ignore, Spacing);
			const bool bShared = CombatGroundRules::IsSharedGround(Profile, Config().MeleeReach.StepMaxCm);
			if (CacheS > 0.f)
			{
				if (SharedGroundCache.Num() >= 1024)
				{
					// Drop the stale ones (other worlds, gone actors, expired); a burst past that clears the lot.
					for (auto It = SharedGroundCache.CreateIterator(); It; ++It)
					{
						const FSharedGroundEntry& Old = It.Value();
						if (Old.World != TObjectKey<UWorld>(&World) || Now < Old.Time || Now - Old.Time >= CacheS)
						{
							It.RemoveCurrent();
						}
					}
					if (SharedGroundCache.Num() >= 1024)
					{
						SharedGroundCache.Reset();
					}
				}
				SharedGroundCache.Add(Key, { TObjectKey<UWorld>(&World), Now, bShared });
			}
			return bShared;
		}
	}

	bool CanMelee(const UWorld& World, const AActor& Attacker, const AActor& Target, float AttackRangeCm)
	{
		const FMeleeReachConfig& Melee = Config().MeleeReach;
		const FVector AttackerFeet = FeetOf(Attacker);
		const FVector TargetFeet = FeetOf(Target);
		const bool bOffGround = EnemyAIRules::CanMeleeOnGround(AttackerFeet, TargetFeet, AttackRangeCm, false, Melee);
		const bool bOnGround = EnemyAIRules::CanMeleeOnGround(AttackerFeet, TargetFeet, AttackRangeCm, true, Melee);
		if (bOffGround == bOnGround)
		{
			return bOnGround; // downhill, flat, out of range or over the cap: the floor changes nothing (no probe)
		}
		return IsSharedGroundBetween(World, Attacker, Target, AttackerFeet, TargetFeet) ? bOnGround : bOffGround;
	}

	float MeleeReachPlanar(const UWorld& World, const AActor& Attacker, const AActor& Target, float AttackRangeCm)
	{
		const FMeleeReachConfig& Melee = Config().MeleeReach;
		const FVector AttackerFeet = FeetOf(Attacker);
		const FVector TargetFeet = FeetOf(Target);
		const float Rise = TargetFeet.Z - AttackerFeet.Z;
		const float OffGround = EnemyAIRules::MeleeReachPlanarOnGround(AttackRangeCm, Rise, false, Melee);
		const float OnGround = EnemyAIRules::MeleeReachPlanarOnGround(AttackRangeCm, Rise, true, Melee);
		if (FMath::IsNearlyEqual(OffGround, OnGround, 1.f))
		{
			return OnGround;
		}
		// Final review 3: past the attack range + both capsule radii no bite and no close-in stop depends on the floor, and 12
		// probes over a long line are metres apart (noise that flipped the acceptance): the shared-ground reach, no probe.
		if (FVector::Dist2D(AttackerFeet, TargetFeet) > AttackRangeCm + Attacker.GetSimpleCollisionRadius() + Target.GetSimpleCollisionRadius())
		{
			return OnGround;
		}
		return IsSharedGroundBetween(World, Attacker, Target, AttackerFeet, TargetFeet) ? OnGround : OffGround;
	}

	bool HasDeadZoneEdge(const UWorld& World, const FVector& ShooterFeet, const FVector& TargetFeet, TConstArrayView<const AActor*> Ignore)
	{
		float Spacing = 0.f;
		const TArray<FFloorSample> Profile = ProbeFloor(World, ShooterFeet, TargetFeet, Ignore, Spacing);
		return CombatGroundRules::HasLevelEdge(Profile, Config().DeadZone.EdgeDropCm, Spacing);
	}

	bool BlocksShot(const UWorld& World, const FVector& ShooterFeet, const FVector& TargetFeet, bool bTraceBlocked, TConstArrayView<const AActor*> Ignore)
	{
		if (!SquadFireRules::IsInDeadZone(ShooterFeet, TargetFeet))
		{
			return false;
		}
		return SquadFireRules::AppliesDeadZone(ShooterFeet, TargetFeet, bTraceBlocked,
			!bTraceBlocked && HasDeadZoneEdge(World, ShooterFeet, TargetFeet, Ignore));
	}
}