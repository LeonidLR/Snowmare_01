#include "Tactics/LookHoldRules.h"
#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace LookHoldRules
{
	namespace
	{
		FLookHoldConfig FromJson(const FJsonObject& Json)
		{
			FLookHoldConfig Config;
			auto ReadClamped = [&Json](const TCHAR* Field, float& Target, float MinVal, float MaxVal)
			{
				double Value = 0.0;
				if (Json.TryGetNumberField(Field, Value))
				{
					Target = FMath::Clamp(static_cast<float>(Value), MinVal, MaxVal);
				}
			};
			ReadClamped(TEXT("min_placement_distance_cm"), Config.MinPlacementDistanceCm, 0.f, 1000.f);
			ReadClamped(TEXT("standing_max_twist_deg"), Config.StandingMaxTwistDeg, 0.f, 90.f);
			ReadClamped(TEXT("prone_max_twist_deg"), Config.ProneMaxTwistDeg, 0.f, 90.f);
			return Config;
		}

		FString GetDefaultPath()
		{
			return FPaths::ProjectContentDir() / TEXT("Data/Tactics/look_hold.json");
		}
	}

	bool LoadConfigFromJson(const FString& JsonString, FLookHoldConfig& OutConfig)
	{
		TSharedPtr<FJsonObject> Json;
		if (FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(JsonString), Json) && Json.IsValid())
		{
			OutConfig = FromJson(*Json);
			return true;
		}
		return false;
	}

	const FLookHoldConfig& GetConfig()
	{
		static FLookHoldConfig Config = []
		{
			FString Text;
			TSharedPtr<FJsonObject> Json;
			return FFileHelper::LoadFileToString(Text, *GetDefaultPath())
				&& FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Json) && Json.IsValid()
				? FromJson(*Json) : FLookHoldConfig();
		}();
		return Config;
	}

	void ReloadConfig()
	{
		const_cast<FLookHoldConfig&>(GetConfig()) = []
		{
			FString Text;
			TSharedPtr<FJsonObject> Json;
			return FFileHelper::LoadFileToString(Text, *GetDefaultPath())
				&& FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Json) && Json.IsValid()
				? FromJson(*Json) : FLookHoldConfig();
		}();
	}

	int32 GetDirectionSector(float RelativeMoveDeg)
	{
		const float Wrapped = FMath::Fmod(FRotator::ClampAxis(RelativeMoveDeg) + 22.5f, 360.f);
		return FMath::Clamp(FMath::FloorToInt(Wrapped / 45.f), 0, 7);
	}

	int32 GetGaitStartSector(int32 LeadSector, bool bBodyHeldByLook)
	{
		return bBodyHeldByLook ? ((LeadSector % 8) + 8) % 8 : 0;
	}

	bool IsOffGaitStartSector(int32 StartSector, float LegDirectionDeg, float ToleranceDeg)
	{
		const float StartDeg = static_cast<float>(((StartSector % 8) + 8) % 8) * 45.f;
		return FMath::Abs(FMath::FindDeltaAngleDegrees(StartDeg, LegDirectionDeg)) > FMath::Max(0.f, ToleranceDeg);
	}

	bool IsValidLookPoint(const FVector& LookPoint, const FVector& OperativeLocation, float MinDistanceCm)
	{
		const float Threshold = MinDistanceCm >= 0.f ? MinDistanceCm : GetConfig().MinPlacementDistanceCm;
		return FVector::DistSquared2D(LookPoint, OperativeLocation) > FMath::Square(FMath::Max(0.f, Threshold));
	}

	bool ShouldClearOnIncapacitated(bool bDead, bool bKnockedDown, bool /*bPanicking*/, bool /*bClimbingLadder*/, bool /*bVaulting*/, bool /*bCarrying*/)
	{
		return bDead || bKnockedDown;
	}

	bool IsLookHoldEligible(bool bInStealthMode, bool bSoloMode, bool bIsLeader)
	{
		if (bInStealthMode)
		{
			return true;
		}
		return bSoloMode || bIsLeader;
	}

	float ClampResidualTwist(float TwistDeg, float LimitDeg)
	{
		const float Limit = LimitDeg >= 0.f ? LimitDeg : GetConfig().StandingMaxTwistDeg;
		return FMath::Clamp(FRotator::NormalizeAxis(TwistDeg), -Limit, Limit);
	}

	FLookDirection DirectionFromClick(const FVector& OperativeLocation, const FVector& ClickPoint)
	{
		const FVector Delta = ClickPoint - OperativeLocation;
		if (Delta.IsNearlyZero())
		{
			return FLookDirection{};
		}
		const FRotator Rot = Delta.Rotation();
		return FLookDirection{ static_cast<float>(FRotator::NormalizeAxis(Rot.Yaw)),
			static_cast<float>(FMath::Clamp(FRotator::NormalizeAxis(Rot.Pitch), -89., 89.)) };
	}

	FVector ProjectLookPoint(const FVector& OperativeLocation, float YawDeg, float PitchDeg, float DistanceCm)
	{
		const FVector Dir = FRotator(FMath::Clamp(PitchDeg, -89.f, 89.f), YawDeg, 0.f).Vector();
		return OperativeLocation + Dir * FMath::Max(DistanceCm, 100.f);
	}
}
