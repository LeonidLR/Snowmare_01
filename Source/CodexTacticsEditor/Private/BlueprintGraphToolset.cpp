#include "BlueprintGraphToolset.h"
#include "Animation/AnimBlueprint.h"
#include "Animation/Skeleton.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "EdGraphUtilities.h"
#include "Engine/Blueprint.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "GameFramework/Actor.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/CompilerResultsLog.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Logging/TokenizedMessage.h"
#include "ScopedTransaction.h"

#define LOCTEXT_NAMESPACE "BlueprintGraphToolset"

namespace
{
	/** '/Game/X/BP_Y' -> '/Game/X/BP_Y.BP_Y' (object path LoadObject expects). */
	FString ToObjectPath(const FString& AssetPath)
	{
		FString Path = AssetPath.TrimStartAndEnd();
		if (!Path.Contains(TEXT(".")))
		{
			Path += TEXT(".") + FPackageName::GetShortName(Path);
		}
		return Path;
	}

	UBlueprint* LoadBlueprint(const FString& AssetPath, FString& OutError)
	{
		UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *ToObjectPath(AssetPath));
		if (!Blueprint)
		{
			OutError = FString::Printf(TEXT("ERROR: no Blueprint at '%s' (use FindBlueprints for the path)."), *AssetPath);
		}
		return Blueprint;
	}

	/** Top-level graphs, then every sub-graph (state machines, states, transitions, collapsed graphs), depth first. */
	void CollectGraphs(UEdGraph* Graph, int32 Depth, TArray<TPair<UEdGraph*, int32>>& Out, TSet<UEdGraph*>& Seen)
	{
		if (!Graph || Seen.Contains(Graph))
		{
			return;
		}
		Seen.Add(Graph);
		Out.Emplace(Graph, Depth);
		TArray<UEdGraph*> Children = Graph->SubGraphs;
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (Node)
			{
				Children.Append(Node->GetSubGraphs());
			}
		}
		for (UEdGraph* Child : Children)
		{
			CollectGraphs(Child, Depth + 1, Out, Seen);
		}
	}

	TArray<TPair<UEdGraph*, int32>> AllGraphs(UBlueprint* Blueprint)
	{
		TArray<UEdGraph*> Top;
		Top.Append(Blueprint->UbergraphPages);
		Top.Append(Blueprint->FunctionGraphs);
		Top.Append(Blueprint->MacroGraphs);
		Top.Append(Blueprint->DelegateSignatureGraphs);
		TArray<TPair<UEdGraph*, int32>> Out;
		TSet<UEdGraph*> Seen;
		for (UEdGraph* Graph : Top)
		{
			CollectGraphs(Graph, 0, Out, Seen);
		}
		return Out;
	}

	UEdGraph* FindGraph(UBlueprint* Blueprint, const FString& GraphName)
	{
		for (const TPair<UEdGraph*, int32>& Entry : AllGraphs(Blueprint))
		{
			if (Entry.Key->GetName().Equals(GraphName, ESearchCase::IgnoreCase))
			{
				return Entry.Key;
			}
		}
		return nullptr;
	}

	FString PinTypeText(const FEdGraphPinType& Type)
	{
		return UEdGraphSchema_K2::TypeToText(Type).ToString();
	}

	void DumpGraph(UEdGraph* Graph, int32 Depth, FString& Out)
	{
		const FString Indent = FString::ChrN(Depth * 2, TEXT(' '));
		Out += FString::Printf(TEXT("%s=== Graph '%s' (%s, %d nodes)\n"), *Indent, *Graph->GetName(), *Graph->GetClass()->GetName(), Graph->Nodes.Num());
		TMap<const UEdGraphNode*, int32> Index;
		for (int32 I = 0; I < Graph->Nodes.Num(); ++I)
		{
			Index.Add(Graph->Nodes[I], I);
		}
		for (int32 I = 0; I < Graph->Nodes.Num(); ++I)
		{
			const UEdGraphNode* Node = Graph->Nodes[I];
			if (!Node)
			{
				continue;
			}
			Out += FString::Printf(TEXT("%s#%d %s \"%s\" at (%d, %d)"), *Indent, I, *Node->GetClass()->GetName(),
				*Node->GetNodeTitle(ENodeTitleType::ListView).ToString().Replace(TEXT("\n"), TEXT(" | ")), Node->NodePosX, Node->NodePosY);
			if (!Node->NodeComment.IsEmpty())
			{
				Out += FString::Printf(TEXT(" // %s"), *Node->NodeComment.Replace(TEXT("\n"), TEXT(" ")));
			}
			Out += TEXT("\n");
			for (const UEdGraphPin* Pin : Node->Pins)
			{
				if (!Pin || Pin->bHidden)
				{
					continue;
				}
				Out += FString::Printf(TEXT("%s    %s %s : %s"), *Indent, Pin->Direction == EGPD_Input ? TEXT("in ") : TEXT("out"),
					*Pin->PinName.ToString(), *PinTypeText(Pin->PinType));
				const FString Default = Pin->GetDefaultAsString();
				if (!Default.IsEmpty() && Pin->LinkedTo.IsEmpty())
				{
					Out += FString::Printf(TEXT(" = %s"), *Default);
				}
				for (const UEdGraphPin* Linked : Pin->LinkedTo)
				{
					const UEdGraphNode* Other = Linked ? Linked->GetOwningNode() : nullptr;
					const int32* OtherIndex = Other ? Index.Find(Other) : nullptr;
					Out += FString::Printf(TEXT(" -> #%s %s"), OtherIndex ? *FString::FromInt(*OtherIndex) : TEXT("?"),
						Linked ? *Linked->PinName.ToString() : TEXT("?"));
				}
				Out += TEXT("\n");
			}
		}
	}

	FString CompileAndReport(UBlueprint* Blueprint)
	{
		FCompilerResultsLog Results;
		Results.bSilentMode = true;
		FKismetEditorUtilities::CompileBlueprint(Blueprint, EBlueprintCompileOptions::SkipGarbageCollection, &Results);
		const TCHAR* Status = Blueprint->Status == BS_Error ? TEXT("ERROR")
			: (Blueprint->Status == BS_UpToDateWithWarnings ? TEXT("WARNINGS") : TEXT("OK"));
		FString Out = FString::Printf(TEXT("Compile: %s (%d errors, %d warnings)\n"), Status, Results.NumErrors, Results.NumWarnings);
		for (const TSharedRef<FTokenizedMessage>& Message : Results.Messages)
		{
			const TCHAR* Severity = Message->GetSeverity() == EMessageSeverity::Error ? TEXT("error")
				: (Message->GetSeverity() == EMessageSeverity::Warning || Message->GetSeverity() == EMessageSeverity::PerformanceWarning ? TEXT("warning") : TEXT("note"));
			Out += FString::Printf(TEXT("  [%s] %s\n"), Severity, *Message->ToText().ToString());
		}
		return Out;
	}
}

FString UBlueprintGraphToolset::FindBlueprints(const FString& Path, const FString& NameFilter)
{
	IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
	FARFilter Filter;
	Filter.PackagePaths.Add(FName(Path.IsEmpty() ? TEXT("/Game") : *Path));
	Filter.bRecursivePaths = true;
	Filter.ClassPaths.Add(UBlueprint::StaticClass()->GetClassPathName());
	Filter.bRecursiveClasses = true;
	TArray<FAssetData> Assets;
	Registry.GetAssets(Filter, Assets);
	Assets.Sort([](const FAssetData& A, const FAssetData& B) { return A.GetObjectPathString() < B.GetObjectPathString(); });
	FString Out;
	int32 Count = 0;
	for (const FAssetData& Asset : Assets)
	{
		if (!NameFilter.IsEmpty() && !Asset.AssetName.ToString().Contains(NameFilter, ESearchCase::IgnoreCase))
		{
			continue;
		}
		FString Parent;
		Asset.GetTagValue(FBlueprintTags::ParentClassPath, Parent);
		Out += FString::Printf(TEXT("%s | %s | parent %s\n"), *Asset.GetObjectPathString(), *Asset.AssetClassPath.GetAssetName().ToString(),
			Parent.IsEmpty() ? TEXT("?") : *FPackageName::ObjectPathToObjectName(Parent));
		++Count;
	}
	return FString::Printf(TEXT("%d Blueprint(s) under %s\n"), Count, Path.IsEmpty() ? TEXT("/Game") : *Path) + Out;
}

FString UBlueprintGraphToolset::DescribeBlueprint(const FString& AssetPath)
{
	FString Error;
	UBlueprint* Blueprint = LoadBlueprint(AssetPath, Error);
	if (!Blueprint)
	{
		return Error;
	}
	FString Out = FString::Printf(TEXT("%s (%s), parent %s\n"), *Blueprint->GetPathName(), *Blueprint->GetClass()->GetName(),
		Blueprint->ParentClass ? *Blueprint->ParentClass->GetName() : TEXT("none"));
	if (const UAnimBlueprint* AnimBlueprint = Cast<UAnimBlueprint>(Blueprint))
	{
		Out += FString::Printf(TEXT("Target skeleton: %s\n"), AnimBlueprint->TargetSkeleton ? *AnimBlueprint->TargetSkeleton->GetPathName() : TEXT("none"));
	}
	for (const FBPInterfaceDescription& Interface : Blueprint->ImplementedInterfaces)
	{
		Out += FString::Printf(TEXT("Interface: %s\n"), Interface.Interface ? *Interface.Interface->GetName() : TEXT("?"));
	}
	Out += FString::Printf(TEXT("Variables (%d):\n"), Blueprint->NewVariables.Num());
	for (const FBPVariableDescription& Variable : Blueprint->NewVariables)
	{
		Out += FString::Printf(TEXT("  %s : %s"), *Variable.VarName.ToString(), *PinTypeText(Variable.VarType));
		if (!Variable.DefaultValue.IsEmpty())
		{
			Out += FString::Printf(TEXT(" = %s"), *Variable.DefaultValue);
		}
		if (!Variable.Category.IsEmpty())
		{
			Out += FString::Printf(TEXT(" [%s]"), *Variable.Category.ToString());
		}
		Out += TEXT("\n");
	}
	if (Blueprint->SimpleConstructionScript)
	{
		Out += TEXT("Components added in the Blueprint:\n");
		for (const USCS_Node* Node : Blueprint->SimpleConstructionScript->GetAllNodes())
		{
			if (Node)
			{
				Out += FString::Printf(TEXT("  %s : %s (attached to %s)\n"), *Node->GetVariableName().ToString(),
					Node->ComponentClass ? *Node->ComponentClass->GetName() : TEXT("?"),
					Node->ParentComponentOrVariableName.IsNone() ? TEXT("root") : *Node->ParentComponentOrVariableName.ToString());
			}
		}
	}
	if (const AActor* Defaults = Blueprint->GeneratedClass ? Cast<AActor>(Blueprint->GeneratedClass->GetDefaultObject()) : nullptr)
	{
		Out += TEXT("Components of the class defaults (native + Blueprint):\n");
		TInlineComponentArray<UActorComponent*> Components(Defaults);
		for (const UActorComponent* Component : Components)
		{
			Out += FString::Printf(TEXT("  %s : %s\n"), *Component->GetName(), *Component->GetClass()->GetName());
		}
	}
	Out += TEXT("Graphs:\n");
	for (const TPair<UEdGraph*, int32>& Entry : AllGraphs(Blueprint))
	{
		Out += FString::Printf(TEXT("%s%s (%s, %d nodes)\n"), *FString::ChrN(2 + Entry.Value * 2, TEXT(' ')), *Entry.Key->GetName(),
			*Entry.Key->GetClass()->GetName(), Entry.Key->Nodes.Num());
	}
	return Out;
}

FString UBlueprintGraphToolset::DumpBlueprintGraph(const FString& AssetPath, const FString& GraphName)
{
	FString Error;
	UBlueprint* Blueprint = LoadBlueprint(AssetPath, Error);
	if (!Blueprint)
	{
		return Error;
	}
	FString Out;
	if (GraphName.IsEmpty())
	{
		for (const TPair<UEdGraph*, int32>& Entry : AllGraphs(Blueprint))
		{
			DumpGraph(Entry.Key, Entry.Value, Out);
		}
		return Out;
	}
	UEdGraph* Graph = FindGraph(Blueprint, GraphName);
	if (!Graph)
	{
		return FString::Printf(TEXT("ERROR: no graph '%s' in %s (DescribeBlueprint lists them)."), *GraphName, *AssetPath);
	}
	TArray<TPair<UEdGraph*, int32>> Graphs;
	TSet<UEdGraph*> Seen;
	CollectGraphs(Graph, 0, Graphs, Seen);
	for (const TPair<UEdGraph*, int32>& Entry : Graphs)
	{
		DumpGraph(Entry.Key, Entry.Value, Out);
	}
	return Out;
}

FString UBlueprintGraphToolset::ExportGraphNodesText(const FString& AssetPath, const FString& GraphName)
{
	FString Error;
	UBlueprint* Blueprint = LoadBlueprint(AssetPath, Error);
	if (!Blueprint)
	{
		return Error;
	}
	UEdGraph* Graph = FindGraph(Blueprint, GraphName);
	if (!Graph)
	{
		return FString::Printf(TEXT("ERROR: no graph '%s' in %s."), *GraphName, *AssetPath);
	}
	TSet<UObject*> Nodes;
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		if (Node && Node->CanDuplicateNode())
		{
			Nodes.Add(Node);
		}
	}
	FString Text;
	FEdGraphUtilities::ExportNodesToText(Nodes, Text);
	return Text;
}

FString UBlueprintGraphToolset::ImportGraphNodesText(const FString& AssetPath, const FString& GraphName, const FString& NodesText, bool bCompile)
{
	FString Error;
	UBlueprint* Blueprint = LoadBlueprint(AssetPath, Error);
	if (!Blueprint)
	{
		return Error;
	}
	UEdGraph* Graph = FindGraph(Blueprint, GraphName);
	if (!Graph)
	{
		return FString::Printf(TEXT("ERROR: no graph '%s' in %s."), *GraphName, *AssetPath);
	}
	if (!FEdGraphUtilities::CanImportNodesFromText(Graph, NodesText))
	{
		return TEXT("ERROR: the text cannot be pasted into this graph (wrong format or node types this graph does not accept).");
	}
	const FScopedTransaction Transaction(LOCTEXT("ImportNodes", "AI: paste nodes"));
	Graph->Modify();
	TSet<UEdGraphNode*> Imported;
	FEdGraphUtilities::ImportNodesFromText(Graph, NodesText, Imported);
	for (UEdGraphNode* Node : Imported)
	{
		Node->CreateNewGuid();
		Node->PostPasteNode();
	}
	Graph->NotifyGraphChanged();
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	FString Out = FString::Printf(TEXT("Pasted %d node(s) into '%s' (not saved: review and save in the editor).\n"), Imported.Num(), *Graph->GetName());
	if (bCompile)
	{
		Out += CompileAndReport(Blueprint);
	}
	return Out;
}

FString UBlueprintGraphToolset::CompileBlueprint(const FString& AssetPath)
{
	FString Error;
	UBlueprint* Blueprint = LoadBlueprint(AssetPath, Error);
	return Blueprint ? CompileAndReport(Blueprint) : Error;
}

#undef LOCTEXT_NAMESPACE
