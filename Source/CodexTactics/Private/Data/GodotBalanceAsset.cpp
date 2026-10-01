#include "Data/GodotBalanceAsset.h"

#include "UObject/UnrealType.h"

namespace
{
	/** Numeric / bool field of the class named Key (UGameBalanceConfig fields carry the Godot names). */
	const FNumericProperty* FindNumericField(const UClass* Class, FName Key, const FBoolProperty*& OutBool)
	{
		OutBool = nullptr;
		const FProperty* Property = Class ? Class->FindPropertyByName(Key) : nullptr;
		if (!Property)
		{
			return nullptr;
		}
		OutBool = CastField<FBoolProperty>(Property);
		return CastField<FNumericProperty>(Property);
	}
}

float UGodotBalanceAsset::GetNumber(FName Key, float Fallback) const
{
	const FBoolProperty* BoolField = nullptr;
	if (const FNumericProperty* Field = FindNumericField(GetClass(), Key, BoolField))
	{
		const void* Data = Field->ContainerPtrToValuePtr<void>(this);
		return Field->IsFloatingPoint() ? static_cast<float>(Field->GetFloatingPointPropertyValue(Data))
			: static_cast<float>(Field->GetSignedIntPropertyValue(Data));
	}
	if (BoolField)
	{
		return BoolField->GetPropertyValue_InContainer(this) ? 1.f : 0.f;
	}
	const float* Value = Numbers.Find(Key);
	return Value ? *Value : Fallback;
}

void UGodotBalanceAsset::SetNumber(FName Key, float Value)
{
	const FBoolProperty* BoolField = nullptr;
	if (const FNumericProperty* Field = FindNumericField(GetClass(), Key, BoolField))
	{
		void* Data = Field->ContainerPtrToValuePtr<void>(this);
		if (Field->IsFloatingPoint())
		{
			Field->SetFloatingPointPropertyValue(Data, Value);
		}
		else
		{
			Field->SetIntPropertyValue(Data, static_cast<int64>(FMath::RoundToInt(Value)));
		}
		return;
	}
	if (BoolField)
	{
		BoolField->SetPropertyValue_InContainer(this, Value != 0.f);
		return;
	}
	Numbers.Add(Key, Value);
}

bool UGodotBalanceAsset::HasField(FName Key) const
{
	const FBoolProperty* BoolField = nullptr;
	return FindNumericField(GetClass(), Key, BoolField) != nullptr || BoolField != nullptr;
}
