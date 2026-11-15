#include "Combat/DeathRagdollRules.h"
#include "CodexTactics.h"
#include "Engine/SkeletalMesh.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "PhysicsEngine/PhysicsConstraintTemplate.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

FDeathRagdollConfig FDeathRagdollRules::GetDefaultConfig()
{
	FDeathRagdollConfig Config;
	Config.bEnabled = false;

	const FString ConfigPath = FPaths::Combine(FPaths::ProjectContentDir(), TEXT("Data/Combat/death_ragdoll.json"));
	FString JsonContent;
	if (FFileHelper::LoadFileToString(JsonContent, *ConfigPath))
	{
		if (LoadConfigFromJson(JsonContent, Config))
		{
			return Config;
		}
	}

	UE_LOG(LogCodexTactics, Error, TEXT("[DeathRagdoll] Failed to load '%s' - death ragdoll disabled, using LayCorpseDown fallback"), *ConfigPath);
	return Config;
}

bool FDeathRagdollRules::LoadConfigFromJson(const FString& JsonContent, FDeathRagdollConfig& OutConfig)
{
	OutConfig = FDeathRagdollConfig();
	OutConfig.bEnabled = false;

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
	if (RootObject->TryGetNumberField(TEXT("max_launch_speed_cm_s"), Val) && Val > 0.0)
	{
		OutConfig.MaxLaunchSpeedCmS = static_cast<float>(Val);
	}
	if (RootObject->TryGetNumberField(TEXT("max_launch_speed_explosive_cm_s"), Val) && Val > 0.0)
	{
		OutConfig.MaxLaunchSpeedExplosiveCmS = static_cast<float>(Val);
	}
	if (RootObject->TryGetNumberField(TEXT("angular_damping"), Val))
	{
		OutConfig.AngularDamping = static_cast<float>(Val);
	}
	if (RootObject->TryGetNumberField(TEXT("linear_damping"), Val))
	{
		OutConfig.LinearDamping = static_cast<float>(Val);
	}
	if (RootObject->TryGetNumberField(TEXT("upward_impulse_bias"), Val))
	{
		OutConfig.UpwardImpulseBias = static_cast<float>(Val);
	}
	if (RootObject->TryGetNumberField(TEXT("min_damage_factor"), Val))
	{
		OutConfig.MinDamageFactor = static_cast<float>(Val);
	}
	if (RootObject->TryGetNumberField(TEXT("max_damage_factor"), Val))
	{
		OutConfig.MaxDamageFactor = static_cast<float>(Val);
	}
	if (RootObject->TryGetNumberField(TEXT("reference_damage"), Val))
	{
		OutConfig.ReferenceDamage = static_cast<float>(Val);
	}
	if (RootObject->TryGetNumberField(TEXT("pelvis_floor_threshold_fraction"), Val))
	{
		OutConfig.PelvisFloorThresholdFraction = static_cast<float>(Val);
	}

	const TSharedPtr<FJsonObject>* ScalesObj = nullptr;
	if (RootObject->TryGetObjectField(TEXT("impulse_scales"), ScalesObj) && ScalesObj && ScalesObj->IsValid())
	{
		OutConfig.ImpulseScales.Empty();
		auto ReadScale = [&](const FString& Key, EDamageType Type)
		{
			double Scale = 0.0;
			if ((*ScalesObj)->TryGetNumberField(Key, Scale))
			{
				OutConfig.ImpulseScales.Add(Type, static_cast<float>(Scale));
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
		OutConfig.FallbackArchetypes.Empty();
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

	// Add upward bias from JSON config (+0.25 by default) so corpses lift off ground cleanly rather than clipping through floor geometry
	Dir = (Dir + FVector(0.f, 0.f, Config.UpwardImpulseBias)).GetSafeNormal();

	const float BaseScale = Config.ImpulseScales.FindRef(DamageType);
	const float MinFactor = (Config.MinDamageFactor > 0.f) ? Config.MinDamageFactor : 0.5f;
	const float MaxFactor = (Config.MaxDamageFactor >= MinFactor) ? Config.MaxDamageFactor : 3.0f;
	const float RefDamage = FMath::Max(Config.ReferenceDamage, 1.0f);
	const float DamageFactor = FMath::Clamp(DamageAmount / RefDamage, MinFactor, MaxFactor);
	float Magnitude = BaseScale * DamageFactor * FMath::Max(MassScale, 0.1f);

	Magnitude = FMath::Clamp(Magnitude, 0.0f, Config.MaxImpulseMagnitude);

	return Dir * Magnitude;
}

float FDeathRagdollRules::GetMaxLaunchSpeed(EDamageType DamageType, const FDeathRagdollConfig& Config)
{
	return (DamageType == EDamageType::Explosive) ? Config.MaxLaunchSpeedExplosiveCmS : Config.MaxLaunchSpeedCmS;
}

FVector FDeathRagdollRules::ClampLinearVelocity(const FVector& Velocity, float MaxSpeed)
{
	if (MaxSpeed > 0.0f)
	{
		const double SpeedSq = Velocity.SizeSquared();
		const double MaxSpeedSq = FMath::Square(static_cast<double>(MaxSpeed));
		if (SpeedSq > MaxSpeedSq)
		{
			return Velocity.GetSafeNormal() * MaxSpeed;
		}
	}
	return Velocity;
}

float FDeathRagdollRules::CalculatePelvisFloorCorrection(float PelvisZ, float FloorZ, float MaxSinkCm)
{
	const float SinkDepth = FloorZ - PelvisZ;
	if (SinkDepth > MaxSinkCm)
	{
		return SinkDepth;
	}
	return 0.0f;
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
	if (Config.FallbackArchetypes.Contains(ArchetypeId))
	{
		return true;
	}
	const FString QueryStr = ArchetypeId.ToString();
	for (const FName& Fallback : Config.FallbackArchetypes)
	{
		if (Fallback.ToString().Equals(QueryStr, ESearchCase::IgnoreCase))
		{
			return true;
		}
	}
	if (QueryStr.Equals(TEXT("CryoDrone"), ESearchCase::IgnoreCase) ||
	    QueryStr.Equals(TEXT("CRYO_DRONE"), ESearchCase::IgnoreCase))
	{
		for (const FName& Fallback : Config.FallbackArchetypes)
		{
			const FString FbStr = Fallback.ToString();
			if (FbStr.Equals(TEXT("CryoDrone"), ESearchCase::IgnoreCase) ||
			    FbStr.Equals(TEXT("CRYO_DRONE"), ESearchCase::IgnoreCase))
			{
				return true;
			}
		}
	}
	return false;
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
