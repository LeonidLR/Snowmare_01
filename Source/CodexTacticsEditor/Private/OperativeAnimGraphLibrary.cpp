#include "OperativeAnimGraphLibrary.h"

#include "AnimGraphNode_BlendListByBool.h"
#include "AnimGraphNode_BlendSpacePlayer.h"
#include "AnimGraphNode_LayeredBoneBlend.h"
#include "AnimGraphNode_Root.h"
#include "AnimGraphNode_RotateRootBone.h"
#include "AnimGraphNode_StateMachine.h"
#include "AnimGraphNode_StateResult.h"
#include "AnimGraphNode_TransitionResult.h"
#include "AnimStateEntryNode.h"
#include "AnimStateNode.h"
#include "AnimStateTransitionNode.h"
#include "AnimationStateGraph.h"
#include "AnimationStateMachineGraph.h"
#include "AnimationTransitionGraph.h"
#include "AnimGraphNode_SaveCachedPose.h"
#include "AnimGraphNode_SequencePlayer.h"
#include "AnimGraphNode_TwoWayBlend.h"
#include "AnimGraphNode_Slot.h"
#include "AnimGraphNode_UseCachedPose.h"
#include "Animation/AnimBlueprint.h"
#include "Animation/BlendSpace.h"
#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "K2Node_VariableGet.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/CompilerResultsLog.h"
#include "Kismet2/KismetEditorUtilities.h"

namespace OperativeAnimGraph
{
	/** Grid step of the generated layout. */
	constexpr int32 ColumnWidth = 360;
	constexpr int32 RowHeight = 220;

	struct FBuilder
	{
		UEdGraph* Graph = nullptr;
		FString& Report;
		bool bOk = true;

		template <typename TNode>
		TNode* Spawn(int32 Column, int32 Row, TFunctionRef<void(TNode&)> Setup)
		{
			FGraphNodeCreator<TNode> Creator(*Graph);
			TNode* Node = Creator.CreateNode();
			Node->NodePosX = Column * ColumnWidth;
			Node->NodePosY = Row * RowHeight;
			Setup(*Node);
			Creator.Finalize();
			return Node;
		}

		UEdGraphPin* Pin(UEdGraphNode* Node, const TCHAR* Name, EEdGraphPinDirection Direction)
		{
			if (UEdGraphPin* Found = Node->FindPin(FName(Name), Direction))
			{
				return Found;
			}
			TArray<FString> Names;
			for (const UEdGraphPin* Each : Node->Pins)
			{
				Names.Add(Each->PinName.ToString());
			}
			Report += FString::Printf(TEXT("missing pin %s on %s (has: %s)\n"), Name, *Node->GetClass()->GetName(), *FString::Join(Names, TEXT(", ")));
			bOk = false;
			return nullptr;
		}

		void Link(UEdGraphPin* From, UEdGraphPin* To)
		{
			if (!From || !To || !Graph->GetSchema()->TryCreateConnection(From, To))
			{
				Report += FString::Printf(TEXT("could not link %s -> %s\n"), From ? *From->PinName.ToString() : TEXT("?"),
					To ? *To->PinName.ToString() : TEXT("?"));
				bOk = false;
			}
		}

		/** Pose output of From into the named pose input of To. */
		void LinkPose(UEdGraphNode* From, UEdGraphNode* To, const TCHAR* InputName)
		{
			Link(Pin(From, TEXT("Pose"), EGPD_Output), Pin(To, InputName, EGPD_Input));
		}

		/** A getter of an anim instance variable wired into a node input. */
		void BindVariable(FName Variable, UEdGraphNode* To, const TCHAR* InputName)
		{
			UK2Node_VariableGet* Getter = Spawn<UK2Node_VariableGet>(0, 0, [Variable](UK2Node_VariableGet& Node)
			{
				Node.VariableReference.SetSelfMember(Variable);
			});
			UEdGraphPin* Input = Pin(To, InputName, EGPD_Input);
			Getter->NodePosX = To->NodePosX - 240;
			Getter->NodePosY = To->NodePosY + 60 * FMath::Max(0, To->Pins.IndexOfByKey(Input));
			Link(Pin(Getter, *Variable.ToString(), EGPD_Output), Input);
		}

		/** Shows the named hidden-by-default property pins of an anim node. */
		static void ShowPins(UAnimGraphNode_Base* Node, std::initializer_list<const TCHAR*> Properties)
		{
			for (FOptionalPinFromProperty& Optional : Node->ShowPinForProperties)
			{
				for (const TCHAR* Property : Properties)
				{
					if (Optional.PropertyName == Property)
					{
						Optional.bShowPin = true;
					}
				}
			}
			Node->ReconstructNode();
		}

		/** Sequence player whose clip (and optionally play rate) come from anim instance variables. */
		UAnimGraphNode_SequencePlayer* Sequence(int32 Row, FName SequenceVariable, FName RateVariable, int32 Column = 1)
		{
			UAnimGraphNode_SequencePlayer* Node = Spawn<UAnimGraphNode_SequencePlayer>(Column, Row, [](UAnimGraphNode_SequencePlayer&) {});
			ShowPins(Node, {TEXT("Sequence"), TEXT("PlayRate")});
			BindVariable(SequenceVariable, Node, TEXT("Sequence"));
			if (!RateVariable.IsNone())
			{
				BindVariable(RateVariable, Node, TEXT("PlayRate"));
			}
			return Node;
		}

		/** Blend space player driven by Direction and the given speed axis / play rate variables. */
		UAnimGraphNode_BlendSpacePlayer* BlendSpace(UBlendSpace* Asset, int32 Row, FName SpeedVariable, FName RateVariable)
		{
			UAnimGraphNode_BlendSpacePlayer* Node = Spawn<UAnimGraphNode_BlendSpacePlayer>(1, Row, [Asset](UAnimGraphNode_BlendSpacePlayer& Player)
			{
				Player.Node.SetBlendSpace(Asset);
			});
			ShowPins(Node, {TEXT("PlayRate")});
			BindVariable(TEXT("Direction"), Node, TEXT("X"));
			BindVariable(SpeedVariable, Node, TEXT("Y"));
			BindVariable(RateVariable, Node, TEXT("PlayRate"));
			return Node;
		}

		/** Blend Poses by bool: True / False poses switched by Variable with the stance crossfade time. */
		UAnimGraphNode_BlendListByBool* ByBool(FName Variable, int32 Column, int32 Row, float BlendTime, UEdGraphNode* WhenTrue, UEdGraphNode* WhenFalse)
		{
			UAnimGraphNode_BlendListByBool* Node = Spawn<UAnimGraphNode_BlendListByBool>(Column, Row, [BlendTime](UAnimGraphNode_BlendListByBool& Blend)
			{
				// BlendTime is private (folded) on the runtime node: set it the way the details panel does.
				if (const FProperty* Property = FAnimNode_BlendListBase::StaticStruct()->FindPropertyByName(TEXT("BlendTime")))
				{
					for (float& Time : *Property->ContainerPtrToValuePtr<TArray<float>>(&Blend.Node))
					{
						Time = BlendTime;
					}
				}
			});
			BindVariable(Variable, Node, TEXT("bActiveValue"));
			LinkPose(WhenTrue, Node, TEXT("BlendPose_0"));
			LinkPose(WhenFalse, Node, TEXT("BlendPose_1"));
			return Node;
		}
	};
}

namespace OperativeAnimGraph
{
	/** The AnimGraph emptied down to its Output Pose node (nullptr + reason when missing). */
	UAnimGraphNode_Root* ResetAnimGraph(UAnimBlueprint* AnimBlueprint, FString& OutReport)
	{
		UEdGraph* AnimGraph = nullptr;
		for (UEdGraph* Graph : AnimBlueprint->FunctionGraphs)
		{
			if (Graph && Graph->GetFName() == UEdGraphSchema_K2::GN_AnimGraph)
			{
				AnimGraph = Graph;
			}
		}
		TArray<UAnimGraphNode_Root*> Roots;
		if (AnimGraph)
		{
			AnimGraph->GetNodesOfClass(Roots);
		}
		if (!AnimGraph || Roots.IsEmpty())
		{
			OutReport = TEXT("the AnimBlueprint has no AnimGraph / Output Pose");
			return nullptr;
		}
		UAnimGraphNode_Root* Root = Roots[0];
		// Start from an empty graph (the script owns this graph; re-running rebuilds it).
		AnimGraph->Modify();
		for (UEdGraphNode* Node : TArray<UEdGraphNode*>(AnimGraph->Nodes))
		{
			if (Node != Root)
			{
				Node->BreakAllNodeLinks();
				AnimGraph->RemoveNode(Node);
			}
		}
		Root->BreakAllNodeLinks();
		return Root;
	}

	bool CompileAndReport(UAnimBlueprint* AnimBlueprint, UEdGraph* AnimGraph, FString& OutReport)
	{
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(AnimBlueprint);
		FCompilerResultsLog Results;
		FKismetEditorUtilities::CompileBlueprint(AnimBlueprint, EBlueprintCompileOptions::None, &Results);
		for (const TSharedRef<FTokenizedMessage>& Message : Results.Messages)
		{
			if (Message->GetSeverity() <= EMessageSeverity::Warning)
			{
				OutReport += Message->ToText().ToString() + TEXT("\n");
			}
		}
		OutReport += FString::Printf(TEXT("AnimGraph: %d nodes, compile errors %d, warnings %d"), AnimGraph->Nodes.Num(), Results.NumErrors, Results.NumWarnings);
		return Results.NumErrors == 0 && AnimBlueprint->Status != BS_Error;
	}
}

int32 UOperativeAnimGraphLibrary::CountAnimGraphNodes(UAnimBlueprint* AnimBlueprint)
{
	if (!AnimBlueprint)
	{
		return 0;
	}
	for (UEdGraph* Graph : AnimBlueprint->FunctionGraphs)
	{
		if (Graph && Graph->GetFName() == UEdGraphSchema_K2::GN_AnimGraph)
		{
			return Graph->Nodes.FilterByPredicate([](const UEdGraphNode* Node) { return Node && !Node->IsA<UAnimGraphNode_Root>(); }).Num();
		}
	}
	return 0;
}

namespace OperativeAnimGraph
{
	/**
	 * The enemy graph tail after Locomotion: the upper-body slot layered above UpperBodyBone (when both are set; the
	 * locomotion is cached once for the base and the slot), the full-body Slot, the Output Pose; then compiles.
	 */
	bool FinishEnemyGraph(FBuilder& Build, UAnimBlueprint* AnimBlueprint, UAnimGraphNode_Root* Root, UEdGraphNode* Locomotion,
		int32 Column, FName SlotName, FName UpperBodySlotName, FName UpperBodyBone, FString& OutReport)
	{
		UEdGraphNode* BeforeFullBody = Locomotion;
		Root->NodePosX = (Column + 2) * ColumnWidth;
		if (!UpperBodySlotName.IsNone() && !UpperBodyBone.IsNone())
		{
			// Hit reactions while running play above UpperBodyBone only: the legs keep running (TANDEM request 1, no foot
			// sliding).
			Root->NodePosX = (Column + 5) * ColumnWidth;
			UAnimGraphNode_SaveCachedPose* Save = Build.Spawn<UAnimGraphNode_SaveCachedPose>(Column + 1, 1, [](UAnimGraphNode_SaveCachedPose& Node)
			{
				Node.CacheName = TEXT("EnemyLocomotion");
			});
			Build.LinkPose(Locomotion, Save, TEXT("Pose"));
			auto UseCache = [&Build, Save, Column](int32 Row)
			{
				return Build.Spawn<UAnimGraphNode_UseCachedPose>(Column + 2, Row, [Save](UAnimGraphNode_UseCachedPose& Node)
				{
					Node.SaveCachedPoseNode = Save;
				});
			};
			UAnimGraphNode_Slot* UpperSlot = Build.Spawn<UAnimGraphNode_Slot>(Column + 3, 2, [UpperBodySlotName](UAnimGraphNode_Slot& Node)
			{
				Node.Node.SlotName = UpperBodySlotName;
			});
			Build.LinkPose(UseCache(2), UpperSlot, TEXT("Source"));
			UAnimGraphNode_LayeredBoneBlend* Layered = Build.Spawn<UAnimGraphNode_LayeredBoneBlend>(Column + 3, 0, [UpperBodyBone](UAnimGraphNode_LayeredBoneBlend& Node)
			{
				Node.Node.bMeshSpaceRotationBlend = true;
				if (Node.Node.LayerSetup.IsEmpty())
				{
					Node.Node.LayerSetup.AddDefaulted();
				}
				FBranchFilter Filter;
				Filter.BoneName = UpperBodyBone;
				Filter.BlendDepth = 0;
				Node.Node.LayerSetup[0].BranchFilters = {Filter};
			});
			Build.LinkPose(UseCache(0), Layered, TEXT("BasePose"));
			Build.LinkPose(UpperSlot, Layered, TEXT("BlendPoses_0"));
			BeforeFullBody = Layered;
		}
		UAnimGraphNode_Slot* Slot = Build.Spawn<UAnimGraphNode_Slot>(Root->NodePosX / ColumnWidth - 1, 1, [SlotName](UAnimGraphNode_Slot& Node)
		{
			Node.Node.SlotName = SlotName;
		});
		Build.LinkPose(BeforeFullBody, Slot, TEXT("Source"));
		Build.LinkPose(Slot, Root, TEXT("Result"));
		return Build.bOk && CompileAndReport(AnimBlueprint, Root->GetGraph(), OutReport);
	}
}

bool UOperativeAnimGraphLibrary::BuildRifle2LocomotionGraph(UAnimBlueprint* AnimBlueprint, UBlendSpace* WalkBlendSpace, FName FullBodySlotName,
	FName UpperBodySlotName, FName UpperBodyBone, FString& OutReport)
{
	using namespace OperativeAnimGraph;
	OutReport.Reset();
	UAnimGraphNode_Root* Root = AnimBlueprint && WalkBlendSpace ? ResetAnimGraph(AnimBlueprint, OutReport) : nullptr;
	if (!Root)
	{
		OutReport = OutReport.IsEmpty() ? TEXT("missing AnimBlueprint / walk blend space") : OutReport;
		return false;
	}
	Root->NodePosY = RowHeight;
	FBuilder Build{Root->GetGraph(), OutReport};

	// The state machine node; PostPlacedNewNode creates its graph with the entry node.
	UAnimGraphNode_StateMachine* Machine = Build.Spawn<UAnimGraphNode_StateMachine>(1, 1, [](UAnimGraphNode_StateMachine&) {});
	UAnimationStateMachineGraph* MachineGraph = Machine->EditorStateMachineGraph;
	if (!MachineGraph || !MachineGraph->EntryNode)
	{
		OutReport += TEXT("the state machine has no graph / entry node");
		return false;
	}
	Machine->OnRenameNode(TEXT("Rifle2Locomotion"));

	// One state: a node created in the machine graph (it creates its own state graph), named, at a grid spot.
	auto AddState = [&](const TCHAR* Name, int32 X, int32 Y) -> UAnimStateNode*
	{
		FGraphNodeCreator<UAnimStateNode> Creator(*MachineGraph);
		UAnimStateNode* State = Creator.CreateNode();
		State->NodePosX = X * 320;
		State->NodePosY = Y * 200;
		Creator.Finalize();
		State->OnRenameNode(Name);
		return State;
	};
	// A sequence player in the state, its clip from an anim instance variable; one-shot clips do not loop.
	auto FillWithClip = [&](UAnimStateNode* State, FName ClipVariable, bool bLoop)
	{
		UAnimationStateGraph* StateGraph = Cast<UAnimationStateGraph>(State->BoundGraph);
		if (!StateGraph || !StateGraph->GetResultNode())
		{
			OutReport += FString::Printf(TEXT("state %s has no graph\n"), *State->GetStateName());
			Build.bOk = false;
			return;
		}
		FBuilder Sub{StateGraph, OutReport};
		UAnimGraphNode_SequencePlayer* Player = Sub.Sequence(0, ClipVariable, NAME_None, -1);
		if (const FBoolProperty* Loop = CastField<FBoolProperty>(FAnimNode_SequencePlayer::StaticStruct()->FindPropertyByName(TEXT("bLoopAnimation"))))
		{
			Loop->SetPropertyValue_InContainer(&Player->Node, bLoop);
		}
		else
		{
			OutReport += TEXT("no bLoopAnimation on the sequence player\n");
		}
		Sub.Link(Sub.Pin(Player, TEXT("Pose"), EGPD_Output), Sub.Pin(StateGraph->GetResultNode(), TEXT("Result"), EGPD_Input));
		Build.bOk &= Sub.bOk;
	};

	UAnimStateNode* Idle = AddState(TEXT("Idle"), 2, 2);
	UAnimStateNode* IdleBreak = AddState(TEXT("IdleBreak"), 2, 0);
	UAnimStateNode* Turn = AddState(TEXT("TurnInPlace"), 2, 4);
	UAnimStateNode* Start = AddState(TEXT("WalkStart"), 4, 1);
	UAnimStateNode* Walk = AddState(TEXT("Walk"), 6, 2);
	UAnimStateNode* Stop = AddState(TEXT("WalkStop"), 4, 3);
	FillWithClip(Idle, TEXT("LocoIdleClip"), true);
	FillWithClip(IdleBreak, TEXT("LocoBreakClip"), false);
	FillWithClip(Turn, TEXT("LocoTurnClip"), false);
	FillWithClip(Start, TEXT("LocoStartClip"), false);
	FillWithClip(Stop, TEXT("LocoStopClip"), false);
	// Walk: the 8-way blend space.
	if (UAnimationStateGraph* WalkGraph = Cast<UAnimationStateGraph>(Walk->BoundGraph))
	{
		FBuilder Sub{WalkGraph, OutReport};
		UAnimGraphNode_BlendSpacePlayer* Player = Sub.BlendSpace(WalkBlendSpace, 0, TEXT("Rifle2BlendSpeed"), TEXT("Rifle2PlayRate"));
		Sub.Link(Sub.Pin(Player, TEXT("Pose"), EGPD_Output), Sub.Pin(WalkGraph->GetResultNode(), TEXT("Result"), EGPD_Input));
		Build.bOk &= Sub.bOk;
	}

	// Entry -> Idle.
	UEdGraphPin* EntryOut = nullptr;
	for (UEdGraphPin* Each : MachineGraph->EntryNode->Pins)
	{
		EntryOut = Each->Direction == EGPD_Output ? Each : EntryOut;
	}
	if (EntryOut)
	{
		EntryOut->MakeLinkTo(Idle->GetInputPin());
	}
	else
	{
		OutReport += TEXT("entry node without an output pin\n");
		Build.bOk = false;
	}

	// A transition whose rule is one bool of the anim instance (the C++ machine sets exactly one state flag).
	int32 Transitions = 0;
	auto AddTransition = [&](UAnimStateNode* From, UAnimStateNode* To, FName Flag, float Crossfade)
	{
		FGraphNodeCreator<UAnimStateTransitionNode> Creator(*MachineGraph);
		UAnimStateTransitionNode* Transition = Creator.CreateNode();
		Transition->NodePosX = (From->NodePosX + To->NodePosX) / 2;
		Transition->NodePosY = (From->NodePosY + To->NodePosY) / 2;
		Creator.Finalize();
		Transition->CreateConnections(From, To);
		Transition->CrossfadeDuration = Crossfade;
		UAnimationTransitionGraph* RuleGraph = Cast<UAnimationTransitionGraph>(Transition->BoundGraph);
		if (!RuleGraph || !RuleGraph->GetResultNode())
		{
			OutReport += FString::Printf(TEXT("transition %s -> %s has no rule graph\n"), *From->GetStateName(), *To->GetStateName());
			Build.bOk = false;
			return;
		}
		FBuilder Rule{RuleGraph, OutReport};
		Rule.BindVariable(Flag, RuleGraph->GetResultNode(), TEXT("bCanEnterTransition"));
		Build.bOk &= Rule.bOk;
		++Transitions;
	};
	AddTransition(Idle, IdleBreak, TEXT("bLocoIdleBreak"), 0.25f);
	AddTransition(IdleBreak, Idle, TEXT("bLocoIdle"), 0.3f);
	AddTransition(Idle, Turn, TEXT("bLocoTurn"), 0.15f);
	AddTransition(IdleBreak, Turn, TEXT("bLocoTurn"), 0.15f);
	AddTransition(Turn, Idle, TEXT("bLocoIdle"), 0.2f);
	AddTransition(Idle, Start, TEXT("bLocoStart"), 0.15f);
	AddTransition(IdleBreak, Start, TEXT("bLocoStart"), 0.15f);
	AddTransition(Turn, Start, TEXT("bLocoStart"), 0.2f);
	AddTransition(Start, Walk, TEXT("bLocoWalk"), 0.2f);
	AddTransition(Start, Stop, TEXT("bLocoStop"), 0.2f);
	AddTransition(Walk, Stop, TEXT("bLocoStop"), 0.2f);
	AddTransition(Stop, Idle, TEXT("bLocoIdle"), 0.25f);
	AddTransition(Stop, Start, TEXT("bLocoStart"), 0.2f);
	// Missing clips (a start / stop the pack lacks): straight between idle and walk.
	AddTransition(Idle, Walk, TEXT("bLocoWalk"), 0.25f);
	AddTransition(Turn, Walk, TEXT("bLocoWalk"), 0.25f);
	AddTransition(Stop, Walk, TEXT("bLocoWalk"), 0.25f);
	AddTransition(Walk, Idle, TEXT("bLocoIdle"), 0.3f);
	AddTransition(Start, Idle, TEXT("bLocoIdle"), 0.25f);

	// Turn-in-place: the root counter-rotated by RootYawOffset.
	UAnimGraphNode_RotateRootBone* Rotate = Build.Spawn<UAnimGraphNode_RotateRootBone>(2, 1, [](UAnimGraphNode_RotateRootBone&) {});
	Build.LinkPose(Machine, Rotate, TEXT("BasePose"));
	Build.BindVariable(TEXT("RootYawOffset"), Rotate, TEXT("Yaw"));
	OutReport += FString::Printf(TEXT("state machine: 6 states, %d transitions\n"), Transitions);
	return FinishEnemyGraph(Build, AnimBlueprint, Root, Rotate, 2, FullBodySlotName, UpperBodySlotName, UpperBodyBone, OutReport);
}

bool UOperativeAnimGraphLibrary::BuildEnemyLocomotionGraph(UAnimBlueprint* AnimBlueprint, FName SlotName, float BlendTime, FName UpperBodySlotName,
	FName UpperBodyBone, FString& OutReport)
{
	using namespace OperativeAnimGraph;
	OutReport.Reset();
	UAnimGraphNode_Root* Root = AnimBlueprint ? ResetAnimGraph(AnimBlueprint, OutReport) : nullptr;
	if (!Root)
	{
		OutReport = OutReport.IsEmpty() ? TEXT("missing AnimBlueprint") : OutReport;
		return false;
	}
	Root->NodePosY = RowHeight;
	FBuilder Build{Root->GetGraph(), OutReport};
	UEdGraphNode* Idle = Build.Sequence(0, TEXT("IdleAnimation"), NAME_None);
	UEdGraphNode* Walk = Build.Sequence(1, TEXT("WalkAnimation"), TEXT("WalkPlayRate"));
	UEdGraphNode* Run = Build.Sequence(2, TEXT("RunAnimation"), TEXT("RunPlayRate"));
	UEdGraphNode* Moving = Build.ByBool(TEXT("bIsRunning"), 2, 2, BlendTime, Run, Walk);
	UEdGraphNode* Locomotion = Build.ByBool(TEXT("bIsMoving"), 3, 1, BlendTime, Moving, Idle);
	return FinishEnemyGraph(Build, AnimBlueprint, Root, Locomotion, 3, SlotName, UpperBodySlotName, UpperBodyBone, OutReport);
}

bool UOperativeAnimGraphLibrary::BuildMarksmanLocomotionGraph(UAnimBlueprint* AnimBlueprint, FName SlotName, float BlendTime,
	float StanceBlendTime, FName UpperBodySlotName, FName UpperBodyBone, FString& OutReport)
{
	using namespace OperativeAnimGraph;
	OutReport.Reset();
	UAnimGraphNode_Root* Root = AnimBlueprint ? ResetAnimGraph(AnimBlueprint, OutReport) : nullptr;
	if (!Root)
	{
		OutReport = OutReport.IsEmpty() ? TEXT("missing AnimBlueprint") : OutReport;
		return false;
	}
	Root->NodePosY = RowHeight;
	FBuilder Build{Root->GetGraph(), OutReport};
	// Still poses: per stance, relaxed / aiming.
	UEdGraphNode* StandIdle = Build.Sequence(0, TEXT("IdleAnimation"), NAME_None);
	UEdGraphNode* StandAim = Build.Sequence(1, TEXT("StandAimAnimation"), NAME_None);
	UEdGraphNode* CrouchIdle = Build.Sequence(2, TEXT("CrouchIdleAnimation"), NAME_None);
	UEdGraphNode* CrouchAim = Build.Sequence(3, TEXT("CrouchAimAnimation"), NAME_None);
	UEdGraphNode* ProneIdle = Build.Sequence(4, TEXT("ProneIdleAnimation"), NAME_None);
	UEdGraphNode* ProneAim = Build.Sequence(5, TEXT("ProneAimAnimation"), NAME_None);
	UEdGraphNode* Stand = Build.ByBool(TEXT("bIsAiming"), 2, 0, StanceBlendTime, StandAim, StandIdle);
	UEdGraphNode* Crouch = Build.ByBool(TEXT("bIsAiming"), 2, 2, StanceBlendTime, CrouchAim, CrouchIdle);
	UEdGraphNode* Prone = Build.ByBool(TEXT("bIsAiming"), 2, 4, StanceBlendTime, ProneAim, ProneIdle);
	UEdGraphNode* Low = Build.ByBool(TEXT("bIsCrouched"), 3, 1, StanceBlendTime, Crouch, Stand);
	UEdGraphNode* Still = Build.ByBool(TEXT("bIsProne"), 4, 2, StanceBlendTime, Prone, Low);
	// Moving (always standing).
	UEdGraphNode* Walk = Build.Sequence(6, TEXT("WalkAnimation"), TEXT("WalkPlayRate"));
	UEdGraphNode* Run = Build.Sequence(7, TEXT("RunAnimation"), TEXT("RunPlayRate"));
	UEdGraphNode* Moving = Build.ByBool(TEXT("bIsRunning"), 4, 6, BlendTime, Run, Walk);
	UEdGraphNode* Locomotion = Build.ByBool(TEXT("bIsMoving"), 5, 3, BlendTime, Moving, Still);
	return FinishEnemyGraph(Build, AnimBlueprint, Root, Locomotion, 5, SlotName, UpperBodySlotName, UpperBodyBone, OutReport);
}

bool UOperativeAnimGraphLibrary::BuildOperativeLocomotionGraph(UAnimBlueprint* AnimBlueprint, UBlendSpace* StandBlendSpace,
	UBlendSpace* StandAimBlendSpace, UBlendSpace* CrouchBlendSpace, UBlendSpace* CrouchAimBlendSpace, UBlendSpace* ProneBlendSpace,
	UBlendSpace* ProneAimBlendSpace, FName SlotName, FName UpperBodyBone, float StanceBlendTime, FString& OutReport)
{
	using namespace OperativeAnimGraph;
	OutReport.Reset();
	if (!AnimBlueprint || !StandBlendSpace || !StandAimBlendSpace || !CrouchBlendSpace || !CrouchAimBlendSpace)
	{
		OutReport = TEXT("missing AnimBlueprint or blend space");
		return false;
	}
	// Without prone clips the crouch blend spaces stand in (with the crouch speed axis).
	const bool bHasProne = ProneBlendSpace != nullptr;
	ProneAimBlendSpace = ProneAimBlendSpace ? ProneAimBlendSpace : (bHasProne ? ProneBlendSpace : CrouchAimBlendSpace);
	ProneBlendSpace = bHasProne ? ProneBlendSpace : CrouchBlendSpace;
	const FName ProneSpeed = bHasProne ? FName(TEXT("ProneBlendSpeed")) : FName(TEXT("SlowBlendSpeed"));
	const FName ProneRate = bHasProne ? FName(TEXT("PronePlayRate")) : FName(TEXT("SlowPlayRate"));

	UAnimGraphNode_Root* Root = ResetAnimGraph(AnimBlueprint, OutReport);
	if (!Root)
	{
		return false;
	}
	UEdGraph* AnimGraph = Root->GetGraph();
	Root->NodePosX = 11 * ColumnWidth;
	Root->NodePosY = 2 * RowHeight;

	FBuilder Build{AnimGraph, OutReport};

	// Column 1: blend space players; 2: aim switch; 3-4: stance switches.
	UEdGraphNode* Stand = Build.BlendSpace(StandBlendSpace, 0, TEXT("StandBlendSpeed"), TEXT("StandPlayRate"));
	UEdGraphNode* StandAim = Build.BlendSpace(StandAimBlendSpace, 1, TEXT("SlowBlendSpeed"), TEXT("SlowPlayRate"));
	UEdGraphNode* Crouch = Build.BlendSpace(CrouchBlendSpace, 2, TEXT("SlowBlendSpeed"), TEXT("SlowPlayRate"));
	UEdGraphNode* CrouchAim = Build.BlendSpace(CrouchAimBlendSpace, 3, TEXT("SlowBlendSpeed"), TEXT("SlowPlayRate"));
	UEdGraphNode* Prone = Build.BlendSpace(ProneBlendSpace, 4, ProneSpeed, ProneRate);
	UEdGraphNode* ProneAim = Build.BlendSpace(ProneAimBlendSpace, 5, ProneSpeed, ProneRate);

	// Aiming while walking / strafing: the legs keep the standing blend space (its own speed and direction, no
	// fast-forwarded aim walk), the aim blend space only drives the upper body.
	UAnimGraphNode_SaveCachedPose* SaveStand = Build.Spawn<UAnimGraphNode_SaveCachedPose>(1, -2, [](UAnimGraphNode_SaveCachedPose& Node)
	{
		Node.CacheName = TEXT("StandLocomotion");
	});
	Build.LinkPose(Stand, SaveStand, TEXT("Pose"));
	auto UseStand = [&Build, SaveStand](int32 Row)
	{
		return Build.Spawn<UAnimGraphNode_UseCachedPose>(2, Row, [SaveStand](UAnimGraphNode_UseCachedPose& Node)
		{
			Node.SaveCachedPoseNode = SaveStand;
		});
	};
	UAnimGraphNode_LayeredBoneBlend* AimOverLegs = Build.Spawn<UAnimGraphNode_LayeredBoneBlend>(2, 1, [UpperBodyBone](UAnimGraphNode_LayeredBoneBlend& Node)
	{
		Node.Node.bMeshSpaceRotationBlend = true;
		if (Node.Node.LayerSetup.IsEmpty())
		{
			Node.Node.LayerSetup.AddDefaulted();
		}
		FBranchFilter Filter;
		Filter.BoneName = UpperBodyBone;
		Filter.BlendDepth = 0;
		Node.Node.LayerSetup[0].BranchFilters = {Filter};
	});
	Build.LinkPose(UseStand(-1), AimOverLegs, TEXT("BasePose"));
	Build.LinkPose(StandAim, AimOverLegs, TEXT("BlendPoses_0"));
	UEdGraphNode* Standing = Build.ByBool(TEXT("bIsAiming"), 3, 0, 0.2f, AimOverLegs, UseStand(0));
	UEdGraphNode* Crouching = Build.ByBool(TEXT("bIsAiming"), 2, 2, 0.2f, CrouchAim, Crouch);
	UEdGraphNode* Lying = Build.ByBool(TEXT("bIsAiming"), 2, 4, 0.2f, ProneAim, Prone);
	UEdGraphNode* Upright = Build.ByBool(TEXT("bIsCrouching"), 3, 1, StanceBlendTime, Crouching, Standing);
	UEdGraphNode* Locomotion = Build.ByBool(TEXT("bIsProne"), 4, 2, StanceBlendTime, Lying, Upright);

	// Cache the locomotion so the montage slot and the lower body share one evaluation.
	UAnimGraphNode_SaveCachedPose* Save = Build.Spawn<UAnimGraphNode_SaveCachedPose>(5, 2, [](UAnimGraphNode_SaveCachedPose& Node)
	{
		Node.CacheName = TEXT("Locomotion");
	});
	Build.LinkPose(Locomotion, Save, TEXT("Pose"));
	auto UseCache = [&Build, Save](int32 Row)
	{
		return Build.Spawn<UAnimGraphNode_UseCachedPose>(6, Row, [Save](UAnimGraphNode_UseCachedPose& Node)
		{
			Node.SaveCachedPoseNode = Save;
		});
	};
	UEdGraphNode* LowerBody = UseCache(1);
	UAnimGraphNode_Slot* Slot = Build.Spawn<UAnimGraphNode_Slot>(7, 3, [SlotName](UAnimGraphNode_Slot& Node)
	{
		Node.Node.SlotName = SlotName;
	});
	Build.LinkPose(UseCache(3), Slot, TEXT("Source"));

	// Fire / reload montages play on the upper body only; the legs keep the locomotion.
	UAnimGraphNode_LayeredBoneBlend* UpperBody = Build.Spawn<UAnimGraphNode_LayeredBoneBlend>(7, 1, [UpperBodyBone](UAnimGraphNode_LayeredBoneBlend& Node)
	{
		Node.Node.bMeshSpaceRotationBlend = true;
		if (Node.Node.LayerSetup.IsEmpty())
		{
			Node.Node.LayerSetup.AddDefaulted();
		}
		FBranchFilter Filter;
		Filter.BoneName = UpperBodyBone;
		Filter.BlendDepth = 0;
		Node.Node.LayerSetup[0].BranchFilters = {Filter};
	});
	Build.LinkPose(LowerBody, UpperBody, TEXT("BasePose"));
	Build.LinkPose(Slot, UpperBody, TEXT("BlendPoses_0"));

	// Cold layer over everything (Godot ColdBlend between the state machine and the output): the level's idle / walk
	// clips by speed, weighted by ColdVisualWeight (0 while aiming / reloading / crouched / without cold clips).
	UEdGraphNode* ColdIdle = Build.Sequence(5, TEXT("ColdIdleAnimation"), NAME_None, 7);
	UEdGraphNode* ColdWalk = Build.Sequence(6, TEXT("ColdWalkAnimation"), NAME_None, 7);
	auto TwoWay = [&Build](int32 Column, int32 Row, FName AlphaVariable, UEdGraphNode* A, UEdGraphNode* B)
	{
		UAnimGraphNode_TwoWayBlend* Node = Build.Spawn<UAnimGraphNode_TwoWayBlend>(Column, Row, [](UAnimGraphNode_TwoWayBlend&) {});
		Build.LinkPose(A, Node, TEXT("A"));
		Build.LinkPose(B, Node, TEXT("B"));
		Build.BindVariable(AlphaVariable, Node, TEXT("Alpha"));
		return Node;
	};
	UEdGraphNode* ColdPose = TwoWay(8, 5, TEXT("ColdMoveBlend"), ColdIdle, ColdWalk);
	UEdGraphNode* WithCold = TwoWay(9, 2, TEXT("ColdVisualWeight"), UpperBody, ColdPose);

	// Full-body one-shots on top of everything (death; UOperativeAnimInstance::FullBodySlot).
	UAnimGraphNode_Slot* FullBody = Build.Spawn<UAnimGraphNode_Slot>(10, 2, [](UAnimGraphNode_Slot& Node)
	{
		Node.Node.SlotName = TEXT("FullBody");
	});
	Build.LinkPose(WithCold, FullBody, TEXT("Source"));
	Build.LinkPose(FullBody, Root, TEXT("Result"));

	return Build.bOk && CompileAndReport(AnimBlueprint, AnimGraph, OutReport);
}

bool UOperativeAnimGraphLibrary::FillDirectionalBlendSpace(UBlendSpace* BlendSpace, UAnimSequence* Idle,
	const TArray<UAnimSequence*>& Moves, float MaxSpeed, FString& OutReport)
{
	OutReport.Reset();
	if (!BlendSpace || !Idle || Moves.IsEmpty() || !Moves[0] || MaxSpeed <= 0.f)
	{
		OutReport = TEXT("missing blend space, idle or forward clip");
		return false;
	}
	BlendSpace->Modify();

	// Axes like the RifleAnims blend spaces (BlendParameters is protected: set it the way the details panel does).
	if (const FStructProperty* Property = CastField<FStructProperty>(UBlendSpace::StaticClass()->FindPropertyByName(TEXT("BlendParameters"))))
	{
		FBlendParameter* Parameters = Property->ContainerPtrToValuePtr<FBlendParameter>(BlendSpace);
		Parameters[0].DisplayName = TEXT("Direction");
		Parameters[0].Min = -180.f;
		Parameters[0].Max = 180.f;
		Parameters[0].GridNum = 8;
		Parameters[1].DisplayName = TEXT("Speed");
		Parameters[1].Min = 0.f;
		Parameters[1].Max = MaxSpeed;
		Parameters[1].GridNum = 1;
	}
	else
	{
		OutReport = TEXT("BlendParameters not found");
		return false;
	}

	while (BlendSpace->GetNumberOfBlendSamples() > 0)
	{
		BlendSpace->DeleteSample(BlendSpace->GetNumberOfBlendSamples() - 1);
	}
	static const float Directions[] = {0.f, 45.f, 90.f, 135.f, 180.f, -135.f, -90.f, -45.f};
	for (const float Direction : {-180.f, -135.f, -90.f, -45.f, 0.f, 45.f, 90.f, 135.f, 180.f})
	{
		BlendSpace->AddSample(Idle, FVector(Direction, 0.f, 0.f));
	}
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Directions); ++Index)
	{
		UAnimSequence* Clip = Moves.IsValidIndex(Index) && Moves[Index] ? Moves[Index] : Moves[0];
		BlendSpace->AddSample(Clip, FVector(Directions[Index], MaxSpeed, 0.f));
		if (Directions[Index] == 180.f)
		{
			BlendSpace->AddSample(Clip, FVector(-180.f, MaxSpeed, 0.f));
		}
	}
	// PostEditChange alone does not rebuild the runtime triangulation (it only reacts to named property edits); without
	// ResampleData the player outputs the reference pose.
	BlendSpace->ValidateSampleData();
	BlendSpace->ResampleData();
	BlendSpace->PostEditChange();
	BlendSpace->MarkPackageDirty();
	OutReport = FString::Printf(TEXT("%s: %d samples, speed 0..%.0f"), *BlendSpace->GetName(), BlendSpace->GetNumberOfBlendSamples(), MaxSpeed);
	return true;
}
