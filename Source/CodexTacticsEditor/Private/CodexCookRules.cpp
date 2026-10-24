#include "CodexCookRules.h"

#include "Misc/PackageName.h"
#include "UObject/Class.h"
#include "UObject/Package.h"
#include "UObject/SoftObjectPtr.h"
#include "UObject/UObjectIterator.h"
#include "UObject/UnrealType.h"

DEFINE_LOG_CATEGORY_STATIC(LogCodexCook, Log, All);

namespace CodexCookRulesLocal
{
	void CollectFromValue(const FProperty* Property, const void* Value, TSet<FSoftObjectPath>& Out);

	void CollectFromStruct(const UStruct* Struct, const void* Container, TSet<FSoftObjectPath>& Out)
	{
		for (TFieldIterator<FProperty> It(Struct); It; ++It)
		{
			for (int32 Index = 0; Index < It->ArrayDim; ++Index)
			{
				CollectFromValue(*It, It->ContainerPtrToValuePtr<void>(Container, Index), Out);
			}
		}
	}

	void CollectFromValue(const FProperty* Property, const void* Value, TSet<FSoftObjectPath>& Out)
	{
		if (const FSoftObjectProperty* Soft = CastField<FSoftObjectProperty>(Property)) // also FSoftClassProperty
		{
			const FSoftObjectPath Path = static_cast<const FSoftObjectPtr*>(Value)->ToSoftObjectPath();
			if (!Path.IsNull())
			{
				Out.Add(Path);
			}
		}
		else if (const FStructProperty* StructProperty = CastField<FStructProperty>(Property))
		{
			if (StructProperty->Struct == TBaseStructure<FSoftObjectPath>::Get() || StructProperty->Struct == TBaseStructure<FSoftClassPath>::Get())
			{
				const FSoftObjectPath& Path = *static_cast<const FSoftObjectPath*>(Value);
				if (!Path.IsNull())
				{
					Out.Add(Path);
				}
			}
			else
			{
				CollectFromStruct(StructProperty->Struct, Value, Out);
			}
		}
		else if (const FArrayProperty* ArrayProperty = CastField<FArrayProperty>(Property))
		{
			FScriptArrayHelper Helper(ArrayProperty, Value);
			for (int32 Index = 0; Index < Helper.Num(); ++Index)
			{
				CollectFromValue(ArrayProperty->Inner, Helper.GetRawPtr(Index), Out);
			}
		}
		else if (const FMapProperty* MapProperty = CastField<FMapProperty>(Property))
		{
			FScriptMapHelper Helper(MapProperty, Value);
			for (FScriptMapHelper::FIterator It(Helper); It; ++It)
			{
				CollectFromValue(MapProperty->KeyProp, Helper.GetKeyPtr(It), Out);
				CollectFromValue(MapProperty->ValueProp, Helper.GetValuePtr(It), Out);
			}
		}
		else if (const FSetProperty* SetProperty = CastField<FSetProperty>(Property))
		{
			FScriptSetHelper Helper(SetProperty, Value);
			for (FScriptSetHelper::FIterator It(Helper); It; ++It)
			{
				CollectFromValue(SetProperty->ElementProp, Helper.GetElementPtr(It), Out);
			}
		}
	}
}

TSet<FSoftObjectPath> CodexCookRules::CollectNativeSoftReferences()
{
	static const FName RuntimePackage(TEXT("/Script/CodexTactics"));
	TSet<FSoftObjectPath> Found;
	for (TObjectIterator<UClass> It; It; ++It)
	{
		UClass* Class = *It;
		if (!Class->HasAnyClassFlags(CLASS_Native) || Class->GetOutermost()->GetFName() != RuntimePackage)
		{
			continue;
		}
		UObject* Defaults = Class->GetDefaultObject(true);
		if (!Defaults)
		{
			continue;
		}
		CodexCookRulesLocal::CollectFromStruct(Class, Defaults, Found);
		TArray<UObject*> Subobjects;
		Defaults->GetDefaultSubobjects(Subobjects);
		for (const UObject* Subobject : Subobjects)
		{
			CodexCookRulesLocal::CollectFromStruct(Subobject->GetClass(), Subobject, Found);
		}
	}
	TSet<FSoftObjectPath> Game;
	for (const FSoftObjectPath& Path : Found)
	{
		if (Path.GetLongPackageName().StartsWith(TEXT("/Game/")))
		{
			Game.Add(Path);
		}
	}
	return Game;
}

void CodexCookRules::AddNativeSoftReferences(TConstArrayView<const ITargetPlatform*>, TArray<FName>& InOutPackagesToCook, TArray<FName>&)
{
	int32 Added = 0;
	for (const FSoftObjectPath& Path : CollectNativeSoftReferences())
	{
		const FName Package = Path.GetLongPackageFName();
		if (!FPackageName::DoesPackageExist(Package.ToString()))
		{
			UE_LOG(LogCodexCook, Warning, TEXT("C++ default names a missing asset: %s"), *Path.ToString());
			continue;
		}
		if (!InOutPackagesToCook.Contains(Package))
		{
			InOutPackagesToCook.Add(Package);
			++Added;
		}
		UE_LOG(LogCodexCook, Display, TEXT("Cook (C++ default soft reference): %s"), *Package.ToString());
	}
	UE_LOG(LogCodexCook, Display, TEXT("Added %d package(s) named by C++ class defaults"), Added);
}
