#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "NiagaraAuthoringLibrary.generated.h"

class UNiagaraSystem;

/**
 * Editor helper that retunes a Niagara system from Python (UE 5.8 exposes no Niagara graph editing to Python). Used by
 * Scripts/Editor/create_weather_snow_fx.py (environment phase 4b, NS_Weather_Snow; docs/systems/environment-weather.md §12).
 *
 * Names: EmitterName is the emitter's unique name ("SnowFlakes"; empty = the system scripts); ModuleName is the stack module's
 * display name ("ShapeLocation", "SpawnRate"); InputName is the module input without namespace ("Cylinder Radius").
 * Rapid iteration parameters are the stack's constant inputs ("Constants.SnowFlakes.ShapeLocation.Cylinder Height").
 * Every call returns a report line, "ERROR: ..." on failure. Nothing is saved here: the caller compiles (CompileSystem) and saves the asset.
 */
UCLASS()
class UNiagaraAuthoringLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Human-readable dump: emitters (sim target, local space, bounds), user parameters, stack modules per script with their overridden inputs, rapid iteration values. */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Niagara")
	static FString DumpNiagaraSystem(UNiagaraSystem* System);

	/**
	 * Sets a rapid iteration (constant stack input) value on every script of the emitter (or of the system) that has it.
	 * ValueText: "1.5", "1,2,3", "true", "4". ParameterName may be the full "Constants.Emitter.Module.Input" or "Module.Input".
	 */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Niagara")
	static FString SetRapidIterationValue(UNiagaraSystem* System, const FString& EmitterName, const FString& ParameterName, const FString& ValueText);

	/** Adds a user parameter ("User." prefix optional). TypeName: float, int, bool, vector2, vector, vector4, color. */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Niagara")
	static FString AddUserParameter(UNiagaraSystem* System, const FString& Name, const FString& TypeName, const FString& DefaultText);

	/** Renames a user parameter in the exposed store and in every graph that reads it. */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Niagara")
	static FString RenameUserParameter(UNiagaraSystem* System, const FString& OldName, const FString& NewName);

	/** Removes a user parameter from the exposed store (only when no graph reads it). */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Niagara")
	static FString RemoveUserParameter(UNiagaraSystem* System, const FString& Name);

	/** Binds a module (or dynamic input) input to a parameter (e.g. "User.SpawnRate", "Engine.Owner.Position"); a previous link / value is replaced. */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Niagara")
	static FString LinkModuleInput(UNiagaraSystem* System, const FString& EmitterName, const FString& ModuleName, const FString& InputName,
		const FString& LinkedParameterName);

	/**
	 * Replaces a module (or dynamic input) input with a dynamic input script (e.g. /Niagara/DynamicInputs/Multiply/Multiply_Float.Multiply_Float);
	 * returns the new node's name, usable as ModuleName for its own inputs (compile before setting their constants).
	 */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Niagara")
	static FString SetModuleInputDynamicInput(UNiagaraSystem* System, const FString& EmitterName, const FString& ModuleName, const FString& InputName,
		const FString& DynamicInputScriptPath);

	/** Removes the override of a module input: it becomes a local (rapid iteration) value again - compile, then SetRapidIterationValue. */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Niagara")
	static FString ClearModuleInput(UNiagaraSystem* System, const FString& EmitterName, const FString& ModuleName, const FString& InputName);

	/** Enables / disables a stack module. */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Niagara")
	static FString SetModuleEnabled(UNiagaraSystem* System, const FString& EmitterName, const FString& ModuleName, bool bEnabled);

	/** Emitter properties: fixed bounds (half extent around the system, cm; <= 0 = dynamic bounds). */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Niagara")
	static FString SetEmitterFixedBounds(UNiagaraSystem* System, const FString& EmitterName, FVector HalfExtent);

	/** Sets a renderer property by name from text (e.g. Material = /Game/...MI_X.MI_X, Alignment = VelocityAligned). */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Niagara")
	static FString SetRendererProperty(UNiagaraSystem* System, const FString& EmitterName, int32 RendererIndex, const FString& PropertyName,
		const FString& ValueText);

	/** Requests a compile and waits for it (GPU shaders included). */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Niagara")
	static FString CompileSystem(UNiagaraSystem* System);
};
