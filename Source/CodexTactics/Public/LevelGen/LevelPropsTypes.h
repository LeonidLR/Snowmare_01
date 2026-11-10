#pragma once

#include "CoreMinimal.h"
#include "Misc/Optional.h"

class FJsonObject;
class FJsonValue;

/**
 * Data of a props file, Content/Data/LevelGen/props/<map>.props.json (schema codex.levelgen_props/1,
 * docs/design/levelgen/PROPS_LAYERS.md section 1.2). Plain structs, no UObjects. Godot reference: none (UE-only level authoring).
 */

/** Keys the reader does not know. They are kept as-is so a round-trip loses nothing (new optional fields are allowed in /1). */
using FLevelPropsExtra = TMap<FString, TSharedPtr<FJsonValue>>;

/** One prop of a layer's roll, or a hand-added prop (PROPS_LAYERS.md 1.2 "Per object"). Positions are integer cm. */
struct FLevelPropsObject
{
	/** "<layer>_<seed hex8>_<n>" or "hand_<layer>_<n>". */
	FString Id;
	/** E.g. barrel / barrel_explosive, crate_s / crate_l. */
	FString Variant;
	/** [x, y] in cm. */
	FIntPoint Pos = FIntPoint::ZeroValue;
	/** Layout layer name: ground, roof:B_x, floor:B_x:n, cut. Defaults to "ground" when absent. */
	FString WalkLayer = TEXT("ground");
	/** Integer degrees. */
	int32 Yaw = 0;
	bool bLocked = false;
	/** "hand" for editor-added props, else empty. */
	FString Origin;
	/** Kind fields (empty / unset = absent). */
	FString Attach;
	FString Door;
	FString Trigger;
	FString LootTable;
	TOptional<int32> Arc;
	FLevelPropsExtra Extra;
};

/** A hand edit (section 1.2 "Overrides"). */
enum class ELevelPropsOp : uint8
{
	Move,
	Delete,
	Add,
	Set
};

struct FLevelPropsOverride
{
	ELevelPropsOp Op = ELevelPropsOp::Move;
	/** Target id (move / delete / set); for Add it mirrors Object.Id. */
	FString Id;
	TOptional<FIntPoint> Pos;
	TOptional<FString> WalkLayer;
	TOptional<int32> ZOffset;
	TOptional<int32> Yaw;
	/** Add: the full object. */
	FLevelPropsObject Object;
	/** Set: the changed fields (JSON object; a null value clears an optional field). */
	TSharedPtr<FJsonObject> Fields;
	FLevelPropsExtra Extra;
};

/** One layer: rules + seed + lock + the last resolved roll. */
struct FLevelPropsLayer
{
	FString Id;
	/** barrels | crates | turrets | mines | loot. */
	FString Kind;
	bool bEnabled = true;
	uint32 Seed = 0;
	bool bLocked = false;
	FString Preset;
	/** Placement rules, opaque here (interpreted by the placement code, C3+). Null when absent. */
	TSharedPtr<FJsonObject> Rules;
	FString RulesHash;
	TArray<FLevelPropsObject> Objects;
	/** Last placement report, opaque. Null when absent. */
	TSharedPtr<FJsonObject> Report;
	FLevelPropsExtra Extra;
};

/** The whole <map>.props.json (schema codex.levelgen_props/1). */
struct FLevelPropsFile
{
	FString Map;
	FString Layout;
	FString LayoutHash;
	/** Bumped by the writer (TG / editor) on every write; the library never changes it. */
	int32 Revision = 0;
	/** When true the TG and the editor refuse writes. */
	bool bFrozen = false;
	TArray<FLevelPropsLayer> Layers;
	TArray<FLevelPropsOverride> Overrides;
	FLevelPropsExtra Extra;
};

/** An object after the overrides were applied. */
struct FLevelPropsResolvedObject
{
	FString LayerId;
	FLevelPropsObject Object;
	/** From a move override (cm above the walk layer's Z). */
	int32 ZOffsetCm = 0;
	/** Moved / set / hand: treated as locked by a re-roll. */
	bool bPinned = false;
	bool bHand = false;
};

/** A keep-out disk left by a deleted generated object (designer answer 6.1 #2). Stays until the delete override is cleared. */
struct FLevelPropsKeepOut
{
	/** The deleted object's id. */
	FString Id;
	FString LayerId;
	FIntPoint Pos = FIntPoint::ZeroValue;
	/**
	 * The layer's own rules.min_spacing_cm. 0 when the layer's stored rules do not state it: the placement (C3) then uses the
	 * spacing of the merged rules (preset from props_balance.json + rules).
	 */
	int32 RadiusCm = 0;
};

struct FLevelPropsResolved
{
	TArray<FLevelPropsResolvedObject> Objects;
	TArray<FLevelPropsKeepOut> KeepOuts;
};
