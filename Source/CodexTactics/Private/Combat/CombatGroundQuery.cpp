#include "Combat/CombatGroundQuery.h"

#include "Characters/EnemyAIRules.h"
#include "Combat/FireGateRules.h"
#include "Combat/SquadFireRules.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"

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

	bool CanMelee(const UWorld& World, const AActor& Attacker, const AActor& Target, float AttackRangeCm)
	{
		const AActor* Ignore[] = { &Attacker, &Target };
		return CanMeleeAt(World, FeetOf(Attacker), FeetOf(Target), AttackRangeCm, Ignore);
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
		float Spacing = 0.f;
		const AActor* Ignore[] = { &Attacker, &Target };
		const TArray<FFloorSample> Profile = ProbeFloor(World, AttackerFeet, TargetFeet, Ignore, Spacing);
		return CombatGroundRules::IsSharedGround(Profile, Melee.StepMaxCm) ? OnGround : OffGround;
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