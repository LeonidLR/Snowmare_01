#include "Combat/DeathRagdollRules.h"
#include "Engine/SkeletalMesh.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "PhysicsEngine/PhysicsConstraintTemplate.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

FDeathRagdollConfig FDeathRagdollRules::GetDefaultConfig()
{
	FDeathRagdollConfig Config;
	Config.bEnabled = true;
	Config.MaxSimultaneousRagdolls = 4;
	Config.RagdollDelaySeconds = 0.0f;
	Config.MaxSettleTimeoutSeconds = 3.5f;
	Config.LinearSettleThresholdCmS = 15.0f;
	Config.AngularSettleThresholdRadS = 0.25f;
	Config.MaxImpulseMagnitude = 120000.0f;
	Config.AngularDamping = 1.5f;
	Config.LinearDamping = 0.5f;

	Config.ImpulseScales.Add(EDamageType::Kinetic, 45000.0f);
	Config.ImpulseScales.Add(EDamageType::Melee, 25000.0f);
	Config.ImpulseScales.Add(EDamageType::Fire, 15000.0f);
	Config.ImpulseScales.Add(EDamageType::Cryo, 5000.0f);
	Config.ImpulseScales.Add(EDamageType::Energy, 30000.0f);
	Config.ImpulseScales.Add(EDamageType::Explosive, 100000.0f);

	Config.FallbackArchetypes.Add(TEXT("Spitter"));
	Config.FallbackArchetypes.Add(TEXT("CryoDrone"));

	return Config;
}

bool FDeathRagdollRules::LoadConfigFromJson(const FString& JsonContent, FDeathRagdollConfig& OutConfig)
{
	if (JsonContent.IsEmpty())
	{
		return false;
	}

	TSharedPtr<FJsonObject> RootObject;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonContent);
	if (!FJsonSerializer::Deserialize(Reader, RootObject) || !RootObject.IsValid())
	{
		return false;
	}

	OutConfig = GetDefaultConfig();

	RootObject->TryGetBoolField(TEXT("enabled"), OutConfig.bEnabled);

	int32 MaxSim = 0;
	if (RootObject->TryGetNumberField(TEXT("max_simultaneous_ragdolls"), MaxSim))
	{
		OutConfig.MaxSimultaneousRagdolls = FMath::Clamp(MaxSim, 1, 16);
	}

	double Val = 0.0;
	if (RootObject->TryGetNumberField(TEXT("ragdoll_delay_s"), Val))
	{
		OutConfig.RagdollDelaySeconds = static_cast<float>(Val);
	}
	if (RootObject->TryGetNumberField(TEXT("max_settle_timeout_s"), Val))
	{
		OutConfig.MaxSettleTimeoutSeconds = static_cast<float>(Val);
	}
	if (RootObject->TryGetNumberField(TEXT("linear_settle_threshold_cm_s"), Val))
	{
		OutConfig.LinearSettleThresholdCmS = static_cast<float>(Val);
	}
	if (RootObject->TryGetNumberField(TEXT("angular_settle_threshold_rad_s"), Val))
	{
		OutConfig.AngularSettleThresholdRadS = static_cast<float>(Val);
	}
	if (RootObject->TryGetNumberField(TEXT("max_impulse_magnitude"), Val))
	{
		OutConfig.MaxImpulseMagnitude = static_cast<float>(Val);
	}
	if (RootObject->TryGetNumberField(TEXT("angular_damping"), Val))
	{
		OutConfig.AngularDamping = static_cast<float>(Val);
	}
	if (RootObject->TryGetNumberField(TEXT("linear_damping"), Val))
	{
		OutConfig.LinearDamping = static_cast<float>(Val);
	}

	const TSharedPtr<FJsonObject>* ScalesObj = nullptr;
	if (RootObject->TryGetObjectField(TEXT("impulse_scales"), ScalesObj) && ScalesObj && ScalesObj->IsValid())
	{
		auto ReadScale = [&](const FString& Key, EDamageType Type)
		{
			double Scale = 0.0;
			if ((*ScalesObj)->TryGetNumberField(Key, Scale))
			{
				OutConfig.ImpulseScales.FindOrAdd(Type) = static_cast<float>(Scale);
			}
		};
		ReadScale(TEXT("Kinetic"), EDamageType::Kinetic);
		ReadScale(TEXT("Melee"), EDamageType::Melee);
		ReadScale(TEXT("Fire"), EDamageType::Fire);
		ReadScale(TEXT("Cryo"), EDamageType::Cryo);
		ReadScale(TEXT("Energy"), EDamageType::Energy);
		ReadScale(TEXT("Explosive"), EDamageType::Explosive);
	}

	const TSharedPtr<FJsonObject>* OverridesObj = nullptr;
	if (RootObject->TryGetObjectField(TEXT("per_archetype_overrides"), OverridesObj) && OverridesObj && OverridesObj->IsValid())
	{
		for (const auto& Pair : (*OverridesObj)->Values)
		{
			const TSharedPtr<FJsonObject> ArchObj = Pair.Value->AsObject();
			if (ArchObj.IsValid())
			{
				bool bAllow = true;
				if (ArchObj->TryGetBoolField(TEXT("bAllowRagdoll"), bAllow) && !bAllow)
				{
					OutConfig.FallbackArchetypes.Add(FName(*Pair.Key));
				}
			}
		}
	}

	return true;
}

FVector FDeathRagdollRules::CalculateImpulse(
	const FVector& HitDirection,
	float DamageAmount,
	EDamageType DamageType,
	const FDeathRagdollConfig& Config,
	float MassScale)
{
	FVector Dir = HitDirection;
	if (Dir.IsNearlyZero())
	{
		Dir = FVector::UpVector * 0.3f - FVector::ForwardVector;
	}
	Dir.Normalize();

	// Add slight upward bias (0.25) so corpses lift off ground cleanly rather than clipping through floor geometry
	Dir = (Dir + FVector(0.f, 0.f, 0.25f)).GetSafeNormal();

	const float BaseScale = Config.ImpulseScales.FindRef(DamageType);
	const float DamageFactor = FMath::Clamp(DamageAmount / 25.0f, 0.5f, 3.0f);
	float Magnitude = BaseScale * DamageFactor * FMath::Max(MassScale, 0.1f);

	Magnitude = FMath::Clamp(Magnitude, 0.0f, Config.MaxImpulseMagnitude);

	return Dir * Magnitude;
}

bool FDeathRagdollRules::HasBodySettled(
	const FVector& LinearVelocity,
	const FVector& AngularVelocity,
	float BodyAge,
	const FDeathRagdollConfig& Config)
{
	// Forced timeout fallback
	if (BodyAge >= Config.MaxSettleTimeoutSeconds)
	{
		return true;
	}

	const float LinSpeed = LinearVelocity.Size();
	const float AngSpeed = AngularVelocity.Size();

	return (LinSpeed < Config.LinearSettleThresholdCmS) &&
	       (AngSpeed < Config.AngularSettleThresholdRadS);
}

bool FDeathRagdollRules::CanSimulateRagdoll(int32 CurrentActiveRagdolls, const FDeathRagdollConfig& Config)
{
	if (!Config.bEnabled)
	{
		return false;
	}
	return CurrentActiveRagdolls < Config.MaxSimultaneousRagdolls;
}

bool FDeathRagdollRules::ShouldFallbackToPose(FName ArchetypeId, const FDeathRagdollConfig& Config)
{
	if (!Config.bEnabled)
	{
		return true;
	}
	return Config.FallbackArchetypes.Contains(ArchetypeId);
}

FName FDeathRagdollRules::GetDefaultPelvisBoneName()
{
	return FName(TEXT("pelvis"));
}

bool FDeathRagdollRules::ValidatePhysicsAssetSetup(const USkeletalMesh* Mesh, FString& OutErrorMessage)
{
	if (!Mesh)
	{
		OutErrorMessage = TEXT("SkeletalMesh pointer is null.");
		return false;
	}

	const UPhysicsAsset* PhysAsset = Mesh->GetPhysicsAsset();
	if (!PhysAsset)
	{
		OutErrorMessage = FString::Printf(TEXT("SkeletalMesh '%s' has no PhysicsAsset assigned."), *Mesh->GetName());
		return false;
	}

	if (PhysAsset->SkeletalBodySetups.Num() == 0)
	{
		OutErrorMessage = FString::Printf(TEXT("PhysicsAsset '%s' on mesh '%s' contains 0 skeletal bodies."), *PhysAsset->GetName(), *Mesh->GetName());
		return false;
	}

	return true;
}
