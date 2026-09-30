#pragma once

#include "CoreMinimal.h"
#include "ToolsetRegistry/ToolsetDefinition.h"
#include "BlueprintGraphToolset.generated.h"

/// Tools for reading and editing Blueprints of the CodexTactics project (actor Blueprints, Animation Blueprints with
/// their AnimGraph, state machines and transition rules, Widget Blueprints): find them, describe their variables and
/// components, dump any graph as readable text (nodes, pins, default values, links), export / import nodes in the
/// editor's copy-paste text format, and compile with the compiler messages.
UCLASS(MinimalAPI)
class UBlueprintGraphToolset : public UToolsetDefinition
{
	GENERATED_BODY()

public:
	/*
	 * Lists Blueprint assets (Blueprint, AnimBlueprint, WidgetBlueprint, ...) under a content path.
	 * @param Path Content folder to search recursively, e.g. '/Game' or '/Game/Characters'.
	 * @param NameFilter Optional case-insensitive part of the asset name; empty lists all.
	 * @return One line per asset: object path, Blueprint class, parent class.
	 */
	UFUNCTION(meta = (AICallable))
	static FString FindBlueprints(const FString& Path, const FString& NameFilter);

	/*
	 * Describes a Blueprint: type, parent class, interfaces, the target skeleton of an Animation Blueprint,
	 * member variables with their types and defaults, the component tree, and every graph (with sub-graphs such as
	 * state machines, states and transition rules) with its node count.
	 * @param AssetPath Blueprint asset path, e.g. '/Game/Characters/ABP_Operative'.
	 * @return A readable text summary, or an error line.
	 */
	UFUNCTION(meta = (AICallable))
	static FString DescribeBlueprint(const FString& AssetPath);

	/*
	 * Dumps a graph of a Blueprint as text: each node with its index, class, title, comment and position, and its
	 * visible pins with direction, type, default value and links ('-> #3 Pin'). Sub-graphs (state machines, states,
	 * transition rules, composite / collapsed graphs) are dumped after their owner.
	 * @param AssetPath Blueprint asset path.
	 * @param GraphName Graph name as listed by DescribeBlueprint (e.g. 'EventGraph', 'AnimGraph', a state name); empty dumps every graph.
	 * @return The graph text, or an error line.
	 */
	UFUNCTION(meta = (AICallable))
	static FString DumpBlueprintGraph(const FString& AssetPath, const FString& GraphName);

	/*
	 * Exports every node of one graph in the editor's copy-paste text format (what Ctrl+C puts on the clipboard).
	 * @param AssetPath Blueprint asset path.
	 * @param GraphName Graph name as listed by DescribeBlueprint.
	 * @return The node text, or an error line.
	 */
	UFUNCTION(meta = (AICallable))
	static FString ExportGraphNodesText(const FString& AssetPath, const FString& GraphName);

	/*
	 * Pastes nodes in the editor's copy-paste text format into a graph (like Ctrl+V), optionally compiling after.
	 * The asset is left modified but NOT saved: the user reviews and saves it.
	 * @param AssetPath Blueprint asset path.
	 * @param GraphName Graph name as listed by DescribeBlueprint.
	 * @param NodesText Node text (Begin Object ... End Object blocks).
	 * @param bCompile Compile the Blueprint after the paste and report the messages.
	 * @return How many nodes were added, and the compile messages.
	 */
	UFUNCTION(meta = (AICallable))
	static FString ImportGraphNodesText(const FString& AssetPath, const FString& GraphName, const FString& NodesText, bool bCompile);

	/*
	 * Compiles a Blueprint and returns its status and every compiler error / warning / note.
	 * @param AssetPath Blueprint asset path.
	 * @return Status line followed by the messages.
	 */
	UFUNCTION(meta = (AICallable))
	static FString CompileBlueprint(const FString& AssetPath);
};
