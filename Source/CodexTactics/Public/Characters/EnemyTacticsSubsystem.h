#pragma once

#include "CoreMinimal.h"
#include "Characters/EnemyTacticsRules.h"
#include "Subsystems/WorldSubsystem.h"
#include "EnemyTacticsSubsystem.generated.h"

class AEnemyCharacter;
class AOperativeCharacter;

/** One melee enemy's current order from the pack coordinator. */
struct CODEXTACTICS_API FEnemyTacticOrder
{
	TWeakObjectPtr<AOperativeCharacter> Target;
	EEnemyTacticRole Role = EEnemyTacticRole::Direct;
	/** Flank: the point round the target's side; FallBack: where to run. */
	FVector MovePoint = FVector::ZeroVector;
};

/**
 * Real-time pack coordinator of the melee enemies (hound, cutter, frostbitten, brute; UE-only, no Godot reference —
 * see EnemyTacticsRules.h). Every 0.4 s it spreads the enemies over the operatives (wounded / isolated / exposed /
 * turned-away ones preferred, a cap per target so the pack surrounds the squad), sends part of each target's
 * attackers round his flank and breaks the morale of the fragile types (fall back to the pack, come again).
 * Codex.Enemy.* console variables are the Jev AI coach's intelligence knobs; a "[EnemyTactics]" summary line every
 * 10 s feeds it.
 */
UCLASS()
class CODEXTACTICS_API UEnemyTacticsSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	/** The order for Enemy (refreshed at most every 0.4 s); false: no order (tactics off, not a melee type). */
	bool GetOrder(AEnemyCharacter* Enemy, FEnemyTacticOrder& OutOrder);

	/** An enemy died here (morale of the pack mates around). */
	void NotifyEnemyDied(const FVector& Location);
	/** A melee hit landed from behind the operative. */
	void NotifyBackstab() { ++Backstabs; ++SummaryBackstabs; }

	/** The melee archetypes the coordinator commands. */
	static bool IsCoordinated(const AEnemyCharacter* Enemy);

	int32 GetFlankOrders() const { return FlankOrders; }
	int32 GetFallBacks() const { return FallBacks; }
	int32 GetBackstabs() const { return Backstabs; }
	/** Refresh now (smokes). */
	void Refresh();

private:
	TMap<TWeakObjectPtr<AEnemyCharacter>, FEnemyTacticOrder> Orders;
	TMap<TWeakObjectPtr<AEnemyCharacter>, double> FallBackUntil;
	TMap<TWeakObjectPtr<AEnemyCharacter>, double> MoraleCooldownUntil;
	TArray<TPair<FVector, double>> RecentDeaths;
	double LastRefresh = -1000.0;
	double LastSummary = 0.0;
	int32 FlankOrders = 0;
	int32 FallBacks = 0;
	int32 Backstabs = 0;
	int32 SummaryFlanks = 0;
	int32 SummaryFallBacks = 0;
	int32 SummaryBackstabs = 0;
	int32 SummaryRetargets = 0;
};
