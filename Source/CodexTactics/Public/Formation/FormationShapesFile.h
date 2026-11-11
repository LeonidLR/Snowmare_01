#pragma once

#include "CoreMinimal.h"
#include "Formation/FormationCustomRules.h"
#include "Formation/FormationTypes.h"

/**
 * Saved formation shapes (Godot reference: none (UE-only; game designer 2026-11-10, variant B; contract docs/design/FORMATION_CONTRACT.md 19;
 * reference implementation Tools/TrainingGround/src/movement/shapesSchema.ts validateFormationShapes, sim.ts getSavedFormationShape /
 * savedShapeTargets, shapesCoverage.ts mirrorFormationShape)). The Training Ground formation editor saves
 * Content/Data/FormationLab/formation_shapes.json, schema codex.formation_shapes/1:
 *   { "schema": "codex.formation_shapes/1",
 *     "formations": { "<formation>": { "<count 2..4>": { "members": [ { role commander|right|left|rear, fwd cm, right cm, look deg, arc deg? } ] } } },
 *     "custom": { "custom_1..3": { "members": [ { squad 0..3, fwd, right, look, arc? } ] } } }
 * The game reads it: a saved shape for (formation in effect, member count) replaces the formula's slots and looks (FormationShapeRules::Slots,
 * the coverage allocator); without one the formula of formation_config.json stays. wall / hedgehog / blizzard / custom are never taken from
 * "formations" (rule-driven); the guard's fold into the trail column is untouched. "custom" seeds the three custom slots (the player's own
 * stored shape wins). Tests: CodexTactics.Formation.Shapes.*, parity CodexTactics.Formation.Parity.SavedShapes.
 *
 * Pure part: Parse / Find / Mirror / Slots. Runtime part: Get() (lazy, live reload in dev builds by the file's timestamp), Codex.Formation.SavedShapes.
 */

/** One member of a saved formation shape (the leader's frame: fwd along the heading, right = heading + 90). */
struct FFormationShapeMember
{
	EFormationRole Role = EFormationRole::Commander;
	/** Integer cm forward of the leader (negative = behind). */
	int32 Fwd = 0;
	/** Integer cm right of the leader. */
	int32 Right = 0;
	/** Integer deg relative to the heading, (-180, 180]. */
	int32 Look = 0;
	/** Integer deg 10..360 - the member's look sector width; unset = FormationShapes::DefaultArcDeg. */
	TOptional<int32> Arc;
};

/** A saved shape for one member count: Members[0] = the commander at (0, 0), the roles = FormationShapeRules::Roles(count) in any order after him. */
struct FFormationShape
{
	TArray<FFormationShapeMember> Members;
};

/** A parsed codex.formation_shapes/1 document. */
struct CODEXTACTICS_API FFormationShapesFile
{
	/** Lower-case formation name as written ("diamond", "echelon_left", "pair", ...) -> member count -> shape. */
	TMap<FString, TMap<int32, FFormationShape>> Formations;
	/** custom_1..3 (index 0..2); an empty shape = not in the file. Members in file order (members[0] = the anchor, CustomSlots 17.2). */
	TArray<FFormationCustomShape> Custom;

	FFormationShapesFile() { Custom.SetNum(FormationCustomRules::SlotCount); }
	bool IsEmpty() const;
};

namespace FormationShapes
{
	/** "codex.formation_shapes/1". */
	CODEXTACTICS_API const TCHAR* GetSchema();
	/** A member without "arc" covers this sector (shapesSchema.ts DEFAULT_ARC_DEG), deg. */
	inline constexpr int32 DefaultArcDeg = 90;

	/**
	 * Parses and validates a document exactly as validateFormationShapes (TS): schema; formation count keys "2" / "3" / "4", members.length ==
	 * count, members[0] the commander at (0, 0), roles unique and == Roles(count), fwd / right / look integers, look in (-180, 180], arc an integer
	 * 10..360, every member within FormationCustomRules::MaxRadius; custom keys custom_1..3 only, squad 0..3 unique, squad 0 at (0, 0), the same
	 * number rules. Any error rejects the WHOLE document (false, OutErrors; the TS parse throws the same way). A formation name the game does not
	 * know, or one that stays rule-driven (wall / hedgehog / blizzard / custom), is kept out of Out with a warning (OutWarnings), no error.
	 */
	CODEXTACTICS_API bool Parse(const FString& JsonText, FFormationShapesFile& Out, TArray<FString>& OutErrors, TArray<FString>* OutWarnings = nullptr);

	/** The formations a saved shape may replace: diamond, wedge, column, line, echelon_left, echelon_right (never wall / hedgehog / blizzard / custom). */
	CODEXTACTICS_API bool IsShapeable(EFormationKind Kind);

	/**
	 * The saved shape for Kind with MemberCount (2..4; more members use the first 4 roles like the formula), contract 19.2:
	 * 1. formations[Kind's name][count];
	 * 2. the PAIR: wedge / diamond with 2 members (the game's pair, menu 1:30) -> formations.pair[2]; no other formation ever takes the pair;
	 * 3. an echelon not saved -> the other echelon's shape for that count, mirrored (Mirror; bOutMirrored).
	 * Unset: not shapeable, MemberCount < 2, or nothing saved (the formula).
	 */
	CODEXTACTICS_API TOptional<FFormationShape> Find(const FFormationShapesFile& File, EFormationKind Kind, int32 MemberCount, bool* bOutMirrored = nullptr);

	/** shapesCoverage.ts mirrorFormationShape: right -> -right, look -> normYaw(-look) (-180 -> 180), roles left <-> right, arc kept, order kept. */
	CODEXTACTICS_API FFormationShape Mirror(const FFormationShape& Shape);

	/**
	 * sim.ts savedShapeTargets: per member in the shape's order, Position = localToWorld(LeaderPos, LeaderYaw, [fwd, right]), Yaw =
	 * normYaw(LeaderYaw + look); OutArcs (optional, same order) = arc or DefaultArcDeg.
	 */
	CODEXTACTICS_API TArray<FFormationSlot> Slots(const FFormationShape& Shape, const FVector2D& LeaderPos, float LeaderYaw, TArray<float>* OutArcs = nullptr);

	/** Contract name of a role in the file ("commander", "right", "left", "rear"). */
	CODEXTACTICS_API const TCHAR* RoleName(EFormationRole Role);

	// --- Runtime (the game's view of the file) ---

	/** Content/Data/FormationLab/formation_shapes.json. */
	CODEXTACTICS_API FString GetDefaultPath();
	/** The file Get() reads (GetDefaultPath() unless SetPath overrode it). */
	CODEXTACTICS_API FString GetPath();
	/** Reads another file from now on (the smokes' scratch copy); "" = back to GetDefaultPath(). Forces a reload on the next Get(). */
	CODEXTACTICS_API void SetPath(const FString& Path);

	/** Codex.Formation.SavedShapes (default 1): 0 = the file is ignored (the formula everywhere, the custom slots without defaults; A/B). */
	CODEXTACTICS_API bool IsEnabled();

	/**
	 * The shapes in force: the file, read on the first call; in non-shipping builds re-read when its timestamp / size changes (checked at most
	 * every LiveReloadSeconds), so a Training Ground save shows up without a restart. A missing file = no shapes; an invalid one keeps the last
	 * valid content (warnings with the errors). Empty while IsEnabled() is false. A Set() override wins until ClearOverride().
	 */
	CODEXTACTICS_API const FFormationShapesFile& Get();
	/** Live reload check interval, s. */
	inline constexpr double LiveReloadSeconds = 0.5;
	/** Re-reads the file now (true when it was read and valid, or it is missing = empty). */
	CODEXTACTICS_API bool Reload();
	/** Bumps whenever the content in force changes (a reload with other content, Set, ClearOverride). */
	CODEXTACTICS_API uint32 GetRevision();

	/** Tests: these shapes instead of the file (no reload) until ClearOverride. */
	CODEXTACTICS_API void SetOverride(const FFormationShapesFile& File);
	CODEXTACTICS_API void ClearOverride();
}
