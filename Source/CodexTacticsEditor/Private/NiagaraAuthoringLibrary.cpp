#include "NiagaraAuthoringLibrary.h"

#include "EdGraph/EdGraphPin.h"
#include "NiagaraEmitter.h"
#include "NiagaraEmitterHandle.h"
#include "NiagaraGraph.h"
#include "NiagaraNodeFunctionCall.h"
#include "NiagaraNodeInput.h"
#include "NiagaraNodeOutput.h"
#include "NiagaraParameterMapHistory.h"
#include "NiagaraRendererProperties.h"
#include "NiagaraScript.h"
#include "NiagaraScriptSource.h"
#include "NiagaraSystem.h"
#include "NiagaraTypes.h"
#include "ViewModels/Stack/NiagaraParameterHandle.h"
#include "ViewModels/Stack/NiagaraStackGraphUtilities.h"

namespace NiagaraAuthoring
{
	/** The editor graph of the emitter (EmitterName non-empty) or of the system. */
	UNiagaraGraph* FindNiagaraGraph(UNiagaraSystem& System, const FString& EmitterName, FVersionedNiagaraEmitterData** OutEmitterData = nullptr)
	{
		if (EmitterName.IsEmpty())
		{
			UNiagaraScriptSource* Source = System.GetSystemSpawnScript() ? Cast<UNiagaraScriptSource>(System.GetSystemSpawnScript()->GetLatestSource()) : nullptr;
			return Source ? Source->NodeGraph.Get() : nullptr;
		}
		for (const FNiagaraEmitterHandle& Handle : System.GetEmitterHandles())
		{
			if (Handle.GetUniqueInstanceName() == EmitterName || Handle.GetName().ToString() == EmitterName)
			{
				FVersionedNiagaraEmitterData* Data = Handle.GetEmitterData();
				if (OutEmitterData)
				{
					*OutEmitterData = Data;
				}
				UNiagaraScriptSource* Source = Data ? Cast<UNiagaraScriptSource>(Data->GraphSource) : nullptr;
				return Source ? Source->NodeGraph.Get() : nullptr;
			}
		}
		return nullptr;
	}

	/** Every compiled script of the emitter (or the two system scripts). */
	TArray<UNiagaraScript*> FindScripts(UNiagaraSystem& System, const FString& EmitterName)
	{
		TArray<UNiagaraScript*> Scripts;
		if (EmitterName.IsEmpty())
		{
			Scripts.Add(System.GetSystemSpawnScript());
			Scripts.Add(System.GetSystemUpdateScript());
			return Scripts;
		}
		FVersionedNiagaraEmitterData* Data = nullptr;
		FindNiagaraGraph(System, EmitterName, &Data);
		if (Data)
		{
			Data->GetScripts(Scripts, false, false);
		}
		return Scripts;
	}

	bool IsParameterMapPin(const UEdGraphPin* Pin)
	{
		return Pin && Pin->PinType.PinSubCategoryObject.Get() == FNiagaraTypeDefinition::GetParameterMapStruct();
	}

	UEdGraphPin* GetParameterMapInput(UEdGraphNode* Node)
	{
		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (Pin->Direction == EGPD_Input && IsParameterMapPin(Pin))
			{
				return Pin;
			}
		}
		return nullptr;
	}

	/** The modules of one stack, in execution order (walked back from the output node). */
	TArray<UNiagaraNodeFunctionCall*> GetOrderedModules(UNiagaraNodeOutput& Output)
	{
		TArray<UNiagaraNodeFunctionCall*> Modules;
		UEdGraphNode* Node = &Output;
		for (int32 Guard = 0; Guard < 512 && Node; ++Guard)
		{
			UEdGraphPin* In = GetParameterMapInput(Node);
			if (!In || In->LinkedTo.Num() == 0)
			{
				break;
			}
			Node = In->LinkedTo[0]->GetOwningNode();
			if (UNiagaraNodeFunctionCall* Call = Cast<UNiagaraNodeFunctionCall>(Node))
			{
				Modules.Insert(Call, 0);
			}
			else if (Cast<UNiagaraNodeInput>(Node))
			{
				break;
			}
		}
		return Modules;
	}

	UNiagaraNodeFunctionCall* FindModule(UNiagaraGraph& Graph, const FString& ModuleName)
	{
		TArray<UNiagaraNodeOutput*> Outputs;
		Graph.GetNodesOfClass<UNiagaraNodeOutput>(Outputs);
		for (UNiagaraNodeOutput* Output : Outputs)
		{
			for (UNiagaraNodeFunctionCall* Call : GetOrderedModules(*Output))
			{
				if (Call->GetFunctionName() == ModuleName)
				{
					return Call;
				}
			}
		}
		// A dynamic input (its value output feeds an override pin).
		TArray<UNiagaraNodeFunctionCall*> Calls;
		Graph.GetNodesOfClass<UNiagaraNodeFunctionCall>(Calls);
		for (UNiagaraNodeFunctionCall* Call : Calls)
		{
			if (Call->GetFunctionName() != ModuleName)
			{
				continue;
			}
			for (const UEdGraphPin* Pin : Call->Pins)
			{
				if (Pin->Direction == EGPD_Output && !IsParameterMapPin(Pin) && Pin->LinkedTo.Num() > 0)
				{
					return Call;
				}
			}
		}
		return nullptr;
	}

	/**
	 * The override pin of ModuleName.InputName (a stack module or a dynamic input), created when missing, with its previous value
	 * (a parameter read or a dynamic input) unlinked; an orphaned parameter-read node is removed.
	 */
	UEdGraphPin* PrepareOverridePin(UNiagaraSystem* System, const FString& EmitterName, const FString& ModuleName, const FString& InputName,
		UNiagaraGraph*& OutGraph, FNiagaraVariable& OutInput, FString& OutReport)
	{
		OutGraph = System ? FindNiagaraGraph(*System, EmitterName) : nullptr;
		UNiagaraNodeFunctionCall* Module = OutGraph ? FindModule(*OutGraph, ModuleName) : nullptr;
		if (!Module)
		{
			OutReport = FString::Printf(TEXT("module %s not found in '%s'"), *ModuleName, *EmitterName);
			return nullptr;
		}
		TArray<FNiagaraVariable> Inputs;
		FNiagaraStackGraphUtilities::GetStackFunctionInputs(*Module, Inputs, FCompileConstantResolver(),
			FNiagaraStackGraphUtilities::ENiagaraGetStackFunctionInputPinsOptions::ModuleInputsOnly);
		const FName ModuleInputName(*(TEXT("Module.") + InputName));
		const FNiagaraVariable* Input = Inputs.FindByPredicate([&](const FNiagaraVariable& Var) { return Var.GetName() == ModuleInputName; });
		if (!Input)
		{
			FString Names;
			for (const FNiagaraVariable& Var : Inputs)
			{
				Names += Var.GetName().ToString() + TEXT("; ");
			}
			OutReport = FString::Printf(TEXT("%s.%s: no such input (has %s)"), *ModuleName, *InputName, *Names);
			return nullptr;
		}
		OutInput = *Input;
		UNiagaraGraph* CalledGraph = Module->GetCalledGraph();
		const TOptional<FNiagaraVariableMetaData> MetaData = CalledGraph ? CalledGraph->GetMetaData(*Input) : TOptional<FNiagaraVariableMetaData>();
		const FGuid InputGuid = MetaData.IsSet() ? MetaData->GetVariableGuid() : FGuid();
		const FNiagaraParameterHandle Aliased = FNiagaraParameterHandle::CreateAliasedModuleParameterHandle(FNiagaraParameterHandle(Input->GetName()), Module);

		OutGraph->Modify();
		UEdGraphPin& OverridePin = FNiagaraStackGraphUtilities::GetOrCreateStackFunctionInputOverridePin(*Module, Aliased, Input->GetType(), InputGuid, FGuid());
		TArray<UEdGraphPin*> OldLinks = OverridePin.LinkedTo;
		OverridePin.BreakAllPinLinks();
		for (UEdGraphPin* Old : OldLinks)
		{
			UEdGraphNode* OldNode = Old->GetOwningNode();
			bool bStillUsed = false;
			for (const UEdGraphPin* Pin : OldNode->Pins)
			{
				bStillUsed |= Pin->Direction == EGPD_Output && Pin->LinkedTo.Num() > 0;
			}
			if (!bStillUsed && OldNode->GetClass()->GetName() == TEXT("NiagaraNodeParameterMapGet"))
			{
				OldNode->BreakAllNodeLinks();
				OutGraph->RemoveNode(OldNode);
			}
		}
		return &OverridePin;
	}

	/** The override (parameter map set) node feeding a module, if any. */
	UEdGraphNode* GetOverrideNode(UNiagaraNodeFunctionCall& Module)
	{
		UEdGraphPin* In = GetParameterMapInput(&Module);
		if (!In || In->LinkedTo.Num() == 0)
		{
			return nullptr;
		}
		UEdGraphNode* Prev = In->LinkedTo[0]->GetOwningNode();
		return Prev && Prev->GetClass()->GetName() == TEXT("NiagaraNodeParameterMapSet") ? Prev : nullptr;
	}

	FString ValueToString(const FNiagaraTypeDefinition& Type, const uint8* Data)
	{
		if (!Data)
		{
			return TEXT("<null>");
		}
		if (Type == FNiagaraTypeDefinition::GetFloatDef())
		{
			return FString::SanitizeFloat(*reinterpret_cast<const float*>(Data));
		}
		if (Type == FNiagaraTypeDefinition::GetIntDef() || Type.IsEnum())
		{
			return FString::FromInt(*reinterpret_cast<const int32*>(Data));
		}
		if (Type == FNiagaraTypeDefinition::GetBoolDef())
		{
			return *reinterpret_cast<const int32*>(Data) != 0 ? TEXT("true") : TEXT("false");
		}
		const int32 Size = Type.GetSize();
		if ((Size == 8 || Size == 12 || Size == 16) && Type.GetStruct() && !Type.IsDataInterface())
		{
			const float* F = reinterpret_cast<const float*>(Data);
			FString Out;
			for (int32 Index = 0; Index < Size / 4; ++Index)
			{
				Out += (Index ? TEXT(",") : TEXT("")) + FString::SanitizeFloat(F[Index]);
			}
			return Out;
		}
		return FString::Printf(TEXT("<%s>"), *Type.GetName());
	}

	bool ParseValue(const FNiagaraTypeDefinition& Type, const FString& Text, TArray<uint8>& OutBytes)
	{
		const int32 Size = Type.GetSize();
		OutBytes.SetNumZeroed(Size);
		if (Type == FNiagaraTypeDefinition::GetBoolDef())
		{
			const bool bValue = Text.Equals(TEXT("true"), ESearchCase::IgnoreCase) || Text == TEXT("1");
			*reinterpret_cast<int32*>(OutBytes.GetData()) = bValue ? FNiagaraBool::True : FNiagaraBool::False;
			return true;
		}
		if (Type == FNiagaraTypeDefinition::GetIntDef() || Type.IsEnum())
		{
			*reinterpret_cast<int32*>(OutBytes.GetData()) = FCString::Atoi(*Text);
			return true;
		}
		TArray<FString> Parts;
		Text.ParseIntoArray(Parts, TEXT(","));
		if (Parts.Num() * 4 != Size)
		{
			return false;
		}
		float* F = reinterpret_cast<float*>(OutBytes.GetData());
		for (int32 Index = 0; Index < Parts.Num(); ++Index)
		{
			F[Index] = FCString::Atof(*Parts[Index].TrimStartAndEnd());
		}
		return true;
	}

	bool TypeFromName(const FString& TypeName, FNiagaraTypeDefinition& OutType)
	{
		const FString Lower = TypeName.ToLower();
		if (Lower == TEXT("float")) { OutType = FNiagaraTypeDefinition::GetFloatDef(); return true; }
		if (Lower == TEXT("int")) { OutType = FNiagaraTypeDefinition::GetIntDef(); return true; }
		if (Lower == TEXT("bool")) { OutType = FNiagaraTypeDefinition::GetBoolDef(); return true; }
		if (Lower == TEXT("vector2")) { OutType = FNiagaraTypeDefinition::GetVec2Def(); return true; }
		if (Lower == TEXT("vector")) { OutType = FNiagaraTypeDefinition::GetVec3Def(); return true; }
		if (Lower == TEXT("vector4")) { OutType = FNiagaraTypeDefinition::GetVec4Def(); return true; }
		if (Lower == TEXT("color")) { OutType = FNiagaraTypeDefinition::GetColorDef(); return true; }
		if (Lower == TEXT("position")) { OutType = FNiagaraTypeDefinition::GetPositionDef(); return true; }
		return false;
	}

	FString WithUserPrefix(const FString& Name)
	{
		return Name.StartsWith(TEXT("User.")) ? Name : TEXT("User.") + Name;
	}

	const FNiagaraVariableWithOffset* FindStoreVariable(const FNiagaraParameterStore& Store, const FString& Name)
	{
		for (const FNiagaraVariableWithOffset& Var : Store.ReadParameterVariables())
		{
			if (Var.GetName().ToString() == Name)
			{
				return &Var;
			}
		}
		return nullptr;
	}

	TArray<UNiagaraGraph*> AllNiagaraGraphs(UNiagaraSystem& System)
	{
		TArray<UNiagaraGraph*> Graphs;
		if (UNiagaraGraph* SystemGraph = FindNiagaraGraph(System, FString()))
		{
			Graphs.Add(SystemGraph);
		}
		for (const FNiagaraEmitterHandle& Handle : System.GetEmitterHandles())
		{
			if (UNiagaraGraph* Graph = FindNiagaraGraph(System, Handle.GetUniqueInstanceName()))
			{
				Graphs.AddUnique(Graph);
			}
		}
		return Graphs;
	}

	void DumpStack(UNiagaraGraph& Graph, FString& Out)
	{
		TArray<UNiagaraNodeOutput*> Outputs;
		Graph.GetNodesOfClass<UNiagaraNodeOutput>(Outputs);
		for (UNiagaraNodeOutput* Output : Outputs)
		{
			Out += FString::Printf(TEXT("  [%s]\n"), *StaticEnum<ENiagaraScriptUsage>()->GetNameStringByValue(int64(Output->GetUsage())));
			for (UNiagaraNodeFunctionCall* Call : GetOrderedModules(*Output))
			{
				Out += FString::Printf(TEXT("    - %s (%s)%s\n"), *Call->GetFunctionName(),
					Call->FunctionScript ? *Call->FunctionScript->GetPathName() : TEXT("no script"), Call->IsNodeEnabled() ? TEXT("") : TEXT(" DISABLED"));
				if (UEdGraphNode* Override = GetOverrideNode(*Call))
				{
					for (UEdGraphPin* Pin : Override->Pins)
					{
						if (Pin->Direction != EGPD_Input || IsParameterMapPin(Pin) || Pin->PinName.ToString().StartsWith(TEXT("Add")))
						{
							continue;
						}
						FString Value = Pin->DefaultValue;
						if (Pin->LinkedTo.Num() > 0)
						{
							const UEdGraphPin* Src = Pin->LinkedTo[0];
							const UEdGraphNode* SrcNode = Src->GetOwningNode();
							Value = FString::Printf(TEXT("<- %s [%s]"), *Src->PinName.ToString(), *SrcNode->GetNodeTitle(ENodeTitleType::ListView).ToString());
							if (const UNiagaraNodeFunctionCall* Dyn = Cast<UNiagaraNodeFunctionCall>(SrcNode))
							{
								Value = FString::Printf(TEXT("<- dynamic input %s (%s)"), *Dyn->GetFunctionName(),
									Dyn->FunctionScript ? *Dyn->FunctionScript->GetPathName() : TEXT(""));
							}
						}
						Out += FString::Printf(TEXT("        %s = %s\n"), *Pin->PinName.ToString(), *Value);
					}
				}
			}
		}
	}

	void DumpRapidIteration(const TArray<UNiagaraScript*>& Scripts, FString& Out)
	{
		TSet<FName> Seen;
		for (UNiagaraScript* Script : Scripts)
		{
			if (!Script)
			{
				continue;
			}
			for (const FNiagaraVariableWithOffset& Var : Script->RapidIterationParameters.ReadParameterVariables())
			{
				if (Seen.Contains(Var.GetName()))
				{
					continue;
				}
				Seen.Add(Var.GetName());
				Out += FString::Printf(TEXT("    %s (%s) = %s\n"), *Var.GetName().ToString(), *Var.GetType().GetName(),
					*ValueToString(Var.GetType(), Script->RapidIterationParameters.GetParameterData(Var.Offset, Var.GetType())));
			}
		}
	}
}

FString UNiagaraAuthoringLibrary::DumpNiagaraSystem(UNiagaraSystem* System)
{
	using namespace NiagaraAuthoring;
	if (!System)
	{
		return TEXT("no system");
	}
	FString Out = FString::Printf(TEXT("System %s\n"), *System->GetPathName());
	Out += TEXT("User parameters:\n");
	const FNiagaraParameterStore& Exposed = System->GetExposedParameters();
	for (const FNiagaraVariableWithOffset& Var : Exposed.ReadParameterVariables())
	{
		Out += FString::Printf(TEXT("  %s (%s) = %s\n"), *Var.GetName().ToString(), *Var.GetType().GetName(),
			*ValueToString(Var.GetType(), Exposed.GetParameterData(Var.Offset, Var.GetType())));
	}
	Out += TEXT("System stack:\n");
	if (UNiagaraGraph* Graph = FindNiagaraGraph(*System, FString()))
	{
		DumpStack(*Graph, Out);
	}
	Out += TEXT("  rapid iteration:\n");
	DumpRapidIteration(FindScripts(*System, FString()), Out);
	for (const FNiagaraEmitterHandle& Handle : System->GetEmitterHandles())
	{
		const FVersionedNiagaraEmitterData* Data = Handle.GetEmitterData();
		Out += FString::Printf(TEXT("Emitter %s (handle %s) enabled %d\n"), *Handle.GetUniqueInstanceName(), *Handle.GetName().ToString(), Handle.GetIsEnabled() ? 1 : 0);
		if (!Data)
		{
			continue;
		}
		Out += FString::Printf(TEXT("  sim %s, local space %d, bounds mode %d, fixed bounds %s, renderers %d\n"),
			Data->SimTarget == ENiagaraSimTarget::GPUComputeSim ? TEXT("GPU") : TEXT("CPU"), Data->bLocalSpace ? 1 : 0,
			int32(Data->CalculateBoundsMode), *Data->FixedBounds.ToString(), Data->GetRenderers().Num());
		for (const UNiagaraRendererProperties* Renderer : Data->GetRenderers())
		{
			Out += FString::Printf(TEXT("    renderer %s\n"), *GetNameSafe(Renderer ? Renderer->GetClass() : nullptr));
			if (!Renderer)
			{
				continue;
			}
			for (const TCHAR* PropertyName : { TEXT("Material"), TEXT("Alignment"), TEXT("FacingMode"), TEXT("SortMode"), TEXT("SubImageSize"),
				TEXT("bSubImageBlend"), TEXT("MinFacingCameraBlendDistance"), TEXT("MaxFacingCameraBlendDistance"), TEXT("bEnabled") })
			{
				if (const FProperty* Property = Renderer->GetClass()->FindPropertyByName(PropertyName))
				{
					FString Value;
					Property->ExportTextItem_Direct(Value, Property->ContainerPtrToValuePtr<void>(Renderer), nullptr, nullptr, PPF_None);
					Out += FString::Printf(TEXT("      %s = %s\n"), PropertyName, *Value);
				}
			}
		}
		if (UNiagaraGraph* Graph = FindNiagaraGraph(*System, Handle.GetUniqueInstanceName()))
		{
			DumpStack(*Graph, Out);
		}
		Out += TEXT("  rapid iteration:\n");
		DumpRapidIteration(FindScripts(*System, Handle.GetUniqueInstanceName()), Out);
	}
	return Out;
}

static bool SetRapidIterationValueImpl(UNiagaraSystem* System, const FString& EmitterName, const FString& ParameterName,
	const FString& ValueText, FString& OutReport)
{
	using namespace NiagaraAuthoring;
	if (!System)
	{
		OutReport = TEXT("no system");
		return false;
	}
	FString FullName = ParameterName;
	if (!FullName.StartsWith(TEXT("Constants.")))
	{
		FullName = EmitterName.IsEmpty() ? TEXT("Constants.") + ParameterName : FString::Printf(TEXT("Constants.%s.%s"), *EmitterName, *ParameterName);
	}
	int32 Written = 0;
	for (UNiagaraScript* Script : FindScripts(*System, EmitterName))
	{
		if (!Script)
		{
			continue;
		}
		const FNiagaraVariableWithOffset* Var = FindStoreVariable(Script->RapidIterationParameters, FullName);
		if (!Var)
		{
			continue;
		}
		TArray<uint8> Bytes;
		if (!ParseValue(Var->GetType(), ValueText, Bytes))
		{
			OutReport = FString::Printf(TEXT("%s: cannot parse '%s' as %s"), *FullName, *ValueText, *Var->GetType().GetName());
			return false;
		}
		Script->Modify();
		Script->RapidIterationParameters.SetParameterData(Bytes.GetData(), Var->Offset, Bytes.Num());
		++Written;
	}
	if (Written == 0)
	{
		OutReport = FString::Printf(TEXT("%s: not found in the scripts of '%s'"), *FullName, *EmitterName);
		return false;
	}
	System->MarkPackageDirty();
	OutReport = FString::Printf(TEXT("%s = %s (%d scripts)"), *FullName, *ValueText, Written);
	return true;
}

static bool AddUserParameterImpl(UNiagaraSystem* System, const FString& Name, const FString& TypeName, const FString& DefaultText,
	FString& OutReport)
{
	using namespace NiagaraAuthoring;
	FNiagaraTypeDefinition Type;
	if (!System || !TypeFromName(TypeName, Type))
	{
		OutReport = FString::Printf(TEXT("bad system or type '%s'"), *TypeName);
		return false;
	}
	const FString FullName = WithUserPrefix(Name);
	FNiagaraUserRedirectionParameterStore& Exposed = System->GetExposedParameters();
	System->Modify();
	const FNiagaraVariable Var(Type, FName(*FullName));
	if (!FindStoreVariable(Exposed, FullName))
	{
		Exposed.AddParameter(Var, true, false);
	}
	TArray<uint8> Bytes;
	if (!DefaultText.IsEmpty() && ParseValue(Type, DefaultText, Bytes))
	{
		Exposed.SetParameterData(Bytes.GetData(), Var, false);
	}
	System->MarkPackageDirty();
	OutReport = FString::Printf(TEXT("user %s (%s) = %s"), *FullName, *Type.GetName(), *DefaultText);
	return true;
}

static bool RenameUserParameterImpl(UNiagaraSystem* System, const FString& OldName, const FString& NewName, FString& OutReport)
{
	using namespace NiagaraAuthoring;
	if (!System)
	{
		OutReport = TEXT("no system");
		return false;
	}
	const FString OldFull = WithUserPrefix(OldName);
	const FString NewFull = WithUserPrefix(NewName);
	FNiagaraUserRedirectionParameterStore& Exposed = System->GetExposedParameters();
	const FNiagaraVariableWithOffset* Existing = FindStoreVariable(Exposed, OldFull);
	if (!Existing)
	{
		OutReport = FString::Printf(TEXT("%s: no such user parameter"), *OldFull);
		return false;
	}
	if (FindStoreVariable(Exposed, NewFull))
	{
		OutReport = FString::Printf(TEXT("%s already exists"), *NewFull);
		return false;
	}
	const FNiagaraVariable OldVar(Existing->GetType(), Existing->GetName());
	System->Modify();
	Exposed.RenameParameter(OldVar, FName(*NewFull));
	int32 GraphsRenamed = 0;
	for (UNiagaraGraph* Graph : AllNiagaraGraphs(*System))
	{
		Graph->Modify();
		if (Graph->RenameParameter(OldVar, FName(*NewFull), false, nullptr, true))
		{
			++GraphsRenamed;
		}
		Graph->NotifyGraphChanged();
	}
	System->MarkPackageDirty();
	OutReport = FString::Printf(TEXT("%s -> %s (%d graphs)"), *OldFull, *NewFull, GraphsRenamed);
	return true;
}

static bool RemoveUserParameterImpl(UNiagaraSystem* System, const FString& Name, FString& OutReport)
{
	using namespace NiagaraAuthoring;
	if (!System)
	{
		OutReport = TEXT("no system");
		return false;
	}
	const FString FullName = WithUserPrefix(Name);
	FNiagaraUserRedirectionParameterStore& Exposed = System->GetExposedParameters();
	const FNiagaraVariableWithOffset* Existing = FindStoreVariable(Exposed, FullName);
	if (!Existing)
	{
		OutReport = FString::Printf(TEXT("%s: no such user parameter"), *FullName);
		return false;
	}
	for (UNiagaraGraph* Graph : AllNiagaraGraphs(*System))
	{
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			for (UEdGraphPin* Pin : Node->Pins)
			{
				if (Pin->PinName.ToString() == FullName && Pin->LinkedTo.Num() > 0)
				{
					OutReport = FString::Printf(TEXT("%s is still read by %s"), *FullName, *Node->GetNodeTitle(ENodeTitleType::ListView).ToString());
					return false;
				}
			}
		}
	}
	System->Modify();
	Exposed.RemoveParameter(FNiagaraVariableBase(Existing->GetType(), Existing->GetName()));
	System->MarkPackageDirty();
	OutReport = FString::Printf(TEXT("removed %s"), *FullName);
	return true;
}

static bool LinkModuleInputImpl(UNiagaraSystem* System, const FString& EmitterName, const FString& ModuleName, const FString& InputName,
	const FString& LinkedParameterName, FString& OutReport)
{
	using namespace NiagaraAuthoring;
	UNiagaraGraph* Graph = nullptr;
	FNiagaraVariable Input;
	UEdGraphPin* OverridePin = PrepareOverridePin(System, EmitterName, ModuleName, InputName, Graph, Input, OutReport);
	if (!OverridePin)
	{
		return false;
	}
	FNiagaraTypeDefinition LinkedType = Input.GetType();
	if (const FNiagaraVariableWithOffset* UserVar = FindStoreVariable(System->GetExposedParameters(), LinkedParameterName))
	{
		LinkedType = UserVar->GetType();
	}
	const FNiagaraVariableBase Linked(LinkedType, FName(*LinkedParameterName));
	TSet<FNiagaraVariableBase> Known;
	Known.Add(Linked);
	FNiagaraStackGraphUtilities::SetLinkedParameterValueForFunctionInput(*OverridePin, Linked, Known);
	Graph->NotifyGraphChanged();
	System->MarkPackageDirty();
	OutReport = FString::Printf(TEXT("%s.%s <- %s"), *ModuleName, *InputName, *LinkedParameterName);
	return true;
}

static bool SetModuleInputDynamicInputImpl(UNiagaraSystem* System, const FString& EmitterName, const FString& ModuleName,
	const FString& InputName, const FString& DynamicInputScriptPath, FString& OutDynamicInputName, FString& OutReport)
{
	using namespace NiagaraAuthoring;
	UNiagaraScript* DynamicInput = LoadObject<UNiagaraScript>(nullptr, *DynamicInputScriptPath);
	if (!DynamicInput)
	{
		OutReport = FString::Printf(TEXT("dynamic input %s not found"), *DynamicInputScriptPath);
		return false;
	}
	UNiagaraGraph* Graph = nullptr;
	FNiagaraVariable Input;
	UEdGraphPin* OverridePin = PrepareOverridePin(System, EmitterName, ModuleName, InputName, Graph, Input, OutReport);
	if (!OverridePin)
	{
		return false;
	}
	UNiagaraNodeFunctionCall* DynamicNode = nullptr;
	FNiagaraStackGraphUtilities::SetDynamicInputForFunctionInput(*OverridePin, DynamicInput, DynamicNode);
	Graph->NotifyGraphChanged();
	System->MarkPackageDirty();
	OutDynamicInputName = DynamicNode ? DynamicNode->GetFunctionName() : FString();
	OutReport = FString::Printf(TEXT("%s.%s <- dynamic input %s"), *ModuleName, *InputName, *OutDynamicInputName);
	return DynamicNode != nullptr;
}

static bool ClearModuleInputImpl(UNiagaraSystem* System, const FString& EmitterName, const FString& ModuleName, const FString& InputName,
	FString& OutReport)
{
	using namespace NiagaraAuthoring;
	UNiagaraGraph* Graph = nullptr;
	FNiagaraVariable Input;
	UEdGraphPin* OverridePin = PrepareOverridePin(System, EmitterName, ModuleName, InputName, Graph, Input, OutReport);
	if (!OverridePin)
	{
		return false;
	}
	OverridePin->GetOwningNode()->RemovePin(OverridePin);
	Graph->NotifyGraphChanged();
	System->MarkPackageDirty();
	OutReport = FString::Printf(TEXT("%s.%s back to a local value (compile, then set its constant)"), *ModuleName, *InputName);
	return true;
}

static bool SetModuleEnabledImpl(UNiagaraSystem* System, const FString& EmitterName, const FString& ModuleName, bool bEnabled, FString& OutReport)
{
	using namespace NiagaraAuthoring;
	UNiagaraGraph* Graph = System ? FindNiagaraGraph(*System, EmitterName) : nullptr;
	UNiagaraNodeFunctionCall* Module = Graph ? FindModule(*Graph, ModuleName) : nullptr;
	if (!Module)
	{
		OutReport = FString::Printf(TEXT("module %s not found in '%s'"), *ModuleName, *EmitterName);
		return false;
	}
	Graph->Modify();
	FNiagaraStackGraphUtilities::SetModuleIsEnabled(*Module, bEnabled);
	Graph->NotifyGraphChanged();
	System->MarkPackageDirty();
	OutReport = FString::Printf(TEXT("%s.%s enabled %d"), *EmitterName, *ModuleName, bEnabled ? 1 : 0);
	return true;
}

static bool SetEmitterFixedBoundsImpl(UNiagaraSystem* System, const FString& EmitterName, FVector HalfExtent, FString& OutReport)
{
	using namespace NiagaraAuthoring;
	FVersionedNiagaraEmitterData* Data = nullptr;
	if (!System || !FindNiagaraGraph(*System, EmitterName, &Data) || !Data)
	{
		OutReport = FString::Printf(TEXT("emitter '%s' not found"), *EmitterName);
		return false;
	}
	System->Modify();
	if (HalfExtent.GetMin() <= 0.)
	{
		Data->CalculateBoundsMode = ENiagaraEmitterCalculateBoundMode::Dynamic;
	}
	else
	{
		Data->CalculateBoundsMode = ENiagaraEmitterCalculateBoundMode::Fixed;
		Data->FixedBounds = FBox(-HalfExtent, HalfExtent);
	}
	System->MarkPackageDirty();
	OutReport = FString::Printf(TEXT("%s bounds %s"), *EmitterName, *HalfExtent.ToString());
	return true;
}

static bool SetRendererPropertyImpl(UNiagaraSystem* System, const FString& EmitterName, int32 RendererIndex,
	const FString& PropertyName, const FString& ValueText, FString& OutReport)
{
	using namespace NiagaraAuthoring;
	FVersionedNiagaraEmitterData* Data = nullptr;
	if (!System || !FindNiagaraGraph(*System, EmitterName, &Data) || !Data || !Data->GetRenderers().IsValidIndex(RendererIndex))
	{
		OutReport = FString::Printf(TEXT("emitter '%s' renderer %d not found"), *EmitterName, RendererIndex);
		return false;
	}
	UNiagaraRendererProperties* Renderer = Data->GetRenderers()[RendererIndex];
	FProperty* Property = Renderer ? Renderer->GetClass()->FindPropertyByName(FName(*PropertyName)) : nullptr;
	if (!Property)
	{
		OutReport = FString::Printf(TEXT("%s: no property %s"), *GetNameSafe(Renderer), *PropertyName);
		return false;
	}
	Renderer->Modify();
	Renderer->PreEditChange(Property);
	if (!Property->ImportText_Direct(*ValueText, Property->ContainerPtrToValuePtr<void>(Renderer), Renderer, PPF_None))
	{
		OutReport = FString::Printf(TEXT("%s.%s: cannot import '%s'"), *GetNameSafe(Renderer), *PropertyName, *ValueText);
		return false;
	}
	FPropertyChangedEvent Event(Property);
	Renderer->PostEditChangeProperty(Event);
	System->MarkPackageDirty();
	OutReport = FString::Printf(TEXT("%s renderer %d %s = %s"), *EmitterName, RendererIndex, *PropertyName, *ValueText);
	return true;
}

static bool CompileSystemImpl(UNiagaraSystem* System, FString& OutReport)
{
	if (!System)
	{
		OutReport = TEXT("no system");
		return false;
	}
	System->RequestCompile(false);
	System->WaitForCompilationComplete(true, false);
	OutReport = FString::Printf(TEXT("compiled %s, valid %d"), *System->GetName(), System->IsValid() ? 1 : 0);
	return System->IsValid();
}

// Python-facing wrappers: UE Python drops a bool return when a function has out parameters, so every call returns its report text
// ("ERROR: ..." on failure).
namespace NiagaraAuthoring
{
	FString Report(bool bOk, const FString& Text)
	{
		return bOk ? Text : TEXT("ERROR: ") + Text;
	}
}

FString UNiagaraAuthoringLibrary::SetRapidIterationValue(UNiagaraSystem* System, const FString& EmitterName, const FString& ParameterName, const FString& ValueText)
{
	FString Text;
	const bool bOk = SetRapidIterationValueImpl(System, EmitterName, ParameterName, ValueText, Text);
	return NiagaraAuthoring::Report(bOk, Text);
}

FString UNiagaraAuthoringLibrary::AddUserParameter(UNiagaraSystem* System, const FString& Name, const FString& TypeName, const FString& DefaultText)
{
	FString Text;
	const bool bOk = AddUserParameterImpl(System, Name, TypeName, DefaultText, Text);
	return NiagaraAuthoring::Report(bOk, Text);
}

FString UNiagaraAuthoringLibrary::RenameUserParameter(UNiagaraSystem* System, const FString& OldName, const FString& NewName)
{
	FString Text;
	const bool bOk = RenameUserParameterImpl(System, OldName, NewName, Text);
	return NiagaraAuthoring::Report(bOk, Text);
}

FString UNiagaraAuthoringLibrary::RemoveUserParameter(UNiagaraSystem* System, const FString& Name)
{
	FString Text;
	const bool bOk = RemoveUserParameterImpl(System, Name, Text);
	return NiagaraAuthoring::Report(bOk, Text);
}

FString UNiagaraAuthoringLibrary::LinkModuleInput(UNiagaraSystem* System, const FString& EmitterName, const FString& ModuleName, const FString& InputName,
	const FString& LinkedParameterName)
{
	FString Text;
	const bool bOk = LinkModuleInputImpl(System, EmitterName, ModuleName, InputName, LinkedParameterName, Text);
	return NiagaraAuthoring::Report(bOk, Text);
}

FString UNiagaraAuthoringLibrary::SetModuleInputDynamicInput(UNiagaraSystem* System, const FString& EmitterName, const FString& ModuleName,
	const FString& InputName, const FString& DynamicInputScriptPath)
{
	FString Text;
	FString NodeName;
	const bool bOk = SetModuleInputDynamicInputImpl(System, EmitterName, ModuleName, InputName, DynamicInputScriptPath, NodeName, Text);
	return bOk ? NodeName : TEXT("ERROR: ") + Text;
}

FString UNiagaraAuthoringLibrary::ClearModuleInput(UNiagaraSystem* System, const FString& EmitterName, const FString& ModuleName, const FString& InputName)
{
	FString Text;
	const bool bOk = ClearModuleInputImpl(System, EmitterName, ModuleName, InputName, Text);
	return NiagaraAuthoring::Report(bOk, Text);
}

FString UNiagaraAuthoringLibrary::SetModuleEnabled(UNiagaraSystem* System, const FString& EmitterName, const FString& ModuleName, bool bEnabled)
{
	FString Text;
	const bool bOk = SetModuleEnabledImpl(System, EmitterName, ModuleName, bEnabled, Text);
	return NiagaraAuthoring::Report(bOk, Text);
}

FString UNiagaraAuthoringLibrary::SetEmitterFixedBounds(UNiagaraSystem* System, const FString& EmitterName, FVector HalfExtent)
{
	FString Text;
	const bool bOk = SetEmitterFixedBoundsImpl(System, EmitterName, HalfExtent, Text);
	return NiagaraAuthoring::Report(bOk, Text);
}

FString UNiagaraAuthoringLibrary::SetRendererProperty(UNiagaraSystem* System, const FString& EmitterName, int32 RendererIndex, const FString& PropertyName,
	const FString& ValueText)
{
	FString Text;
	const bool bOk = SetRendererPropertyImpl(System, EmitterName, RendererIndex, PropertyName, ValueText, Text);
	return NiagaraAuthoring::Report(bOk, Text);
}

FString UNiagaraAuthoringLibrary::CompileSystem(UNiagaraSystem* System)
{
	FString Text;
	const bool bOk = CompileSystemImpl(System, Text);
	return NiagaraAuthoring::Report(bOk, Text);
}
