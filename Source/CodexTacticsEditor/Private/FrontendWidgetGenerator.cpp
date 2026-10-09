#include "FrontendWidgetGenerator.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Blueprint/WidgetBlueprintGeneratedClass.h"
#include "Blueprint/WidgetTree.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "UI/Frontend/CodexActivatableScreen.h"
#include "UI/Frontend/CodexDefaultTree.h"
#include "UI/Frontend/CodexSaveSlotsScreen.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "WidgetBlueprint.h"

namespace FrontendGenerator
{
	void ApplyClassDefaults(UWidgetBlueprint& Blueprint, UClass* EntryButtonClass, UClass* SlotEntryClass)
	{
		UObject* Defaults = Blueprint.GeneratedClass ? Blueprint.GeneratedClass->GetDefaultObject() : nullptr;
		if (UCodexActivatableScreen* Screen = Cast<UCodexActivatableScreen>(Defaults))
		{
			if (EntryButtonClass && EntryButtonClass->IsChildOf(UCodexMenuButton::StaticClass()))
			{
				Screen->EntryButtonClass = EntryButtonClass;
			}
			if (UCodexSaveSlotsScreen* Slots = Cast<UCodexSaveSlotsScreen>(Screen); Slots && SlotEntryClass && SlotEntryClass->IsChildOf(UCodexSaveSlotEntry::StaticClass()))
			{
				Slots->SlotEntryClass = SlotEntryClass;
			}
			Screen->MarkPackageDirty();
		}
	}

	void ClearTree(UWidgetTree& Tree)
	{
		TArray<UWidget*> Widgets;
		Tree.GetAllWidgets(Widgets);
		for (UWidget* Widget : Widgets)
		{
			if (Widget)
			{
				Widget->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors | REN_NonTransactional);
			}
		}
		Tree.RootWidget = nullptr;
	}
}

UWidgetBlueprint* UFrontendWidgetGenerator::BuildFrontendWidget(const FString& PackagePath, const FString& AssetName, UClass* ParentClass,
	UClass* EntryButtonClass, UClass* SlotEntryClass, bool bRebuildTree, FString& OutReport)
{
	if (!ParentClass || !ParentClass->ImplementsInterface(UCodexDefaultTree::StaticClass()))
	{
		OutReport = TEXT("parent class missing or not an ICodexDefaultTree");
		return nullptr;
	}
	const FString PackageName = PackagePath / AssetName;
	UWidgetBlueprint* Blueprint = LoadObject<UWidgetBlueprint>(nullptr, *(PackageName + TEXT(".") + AssetName), nullptr, LOAD_NoWarn | LOAD_Quiet);
	if (!Blueprint)
	{
		UPackage* Package = CreatePackage(*PackageName);
		Blueprint = Cast<UWidgetBlueprint>(FKismetEditorUtilities::CreateBlueprint(ParentClass, Package, FName(*AssetName), BPTYPE_Normal,
			UWidgetBlueprint::StaticClass(), UWidgetBlueprintGeneratedClass::StaticClass()));
		if (!Blueprint)
		{
			OutReport = TEXT("CreateBlueprint failed");
			return nullptr;
		}
		FAssetRegistryModule::AssetCreated(Blueprint);
		OutReport += TEXT("created; ");
	}
	else if (Blueprint->ParentClass != ParentClass)
	{
		OutReport = FString::Printf(TEXT("exists with parent %s (expected %s) - left alone"), *GetNameSafe(Blueprint->ParentClass), *ParentClass->GetName());
		return Blueprint;
	}

	FrontendGenerator::ApplyClassDefaults(*Blueprint, EntryButtonClass, SlotEntryClass);
	UWidgetTree* Tree = Blueprint->WidgetTree;
	const bool bEmpty = !Tree || !Tree->RootWidget;
	if (Tree && (bEmpty || bRebuildTree))
	{
		FrontendGenerator::ClearTree(*Tree);
		const ICodexDefaultTree* Builder = Cast<ICodexDefaultTree>(Blueprint->GeneratedClass ? Blueprint->GeneratedClass->GetDefaultObject() : ParentClass->GetDefaultObject());
		if (!Builder)
		{
			Builder = Cast<ICodexDefaultTree>(ParentClass->GetDefaultObject());
		}
		Builder->BuildDefaultTree(*Tree);
		OutReport += TEXT("tree built; ");
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	}
	else
	{
		OutReport += TEXT("tree kept; ");
	}
	FKismetEditorUtilities::CompileBlueprint(Blueprint);
	FrontendGenerator::ApplyClassDefaults(*Blueprint, EntryButtonClass, SlotEntryClass);
	Blueprint->MarkPackageDirty();
	OutReport += Blueprint->Status == BS_Error ? TEXT("COMPILE ERROR") : TEXT("compiled");
	return Blueprint;
}
