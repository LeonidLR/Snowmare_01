#include "AI/PmcCommanderRules.h"

#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace PmcCommanderRules
{
	namespace PmcCommanderLocal
	{
		inline float Clamp01(float Value)
		{
			return FMath::Clamp(Value, 0.f, 1.f);
		}

		inline float Step(bool bCondition)
		{
			return bCondition ? 1.f : 0.f;
		}

		inline void ReadNumber(const FJsonObject& Json, const TCHAR* Key, float& Out)
		{
			double Value = 0.0;
			if (Json.TryGetNumberField(Key, Value))
			{
				Out = static_cast<float>(Value);
			}
		}
	}

	const TCHAR* OrderName(EPmcOrder Order)
	{
		switch (Order)
		{
		case EPmcOrder::Hold: return TEXT("Hold");
		case EPmcOrder::TakeCover: return TEXT("TakeCover");
		case EPmcOrder::Advance: return TEXT("Advance");
		case EPmcOrder::Flank: return TEXT("Flank");
		case EPmcOrder::Suppress: return TEXT("Suppress");
		case EPmcOrder::FallBack: return TEXT("FallBack");
		case EPmcOrder::Heal: return TEXT("Heal");
		case EPmcOrder::Relocate: return TEXT("Relocate");
		case EPmcOrder::Ambush: return TEXT("Ambush");
		case EPmcOrder::Reload: return TEXT("Reload");
		case EPmcOrder::Sidearm: return TEXT("Sidearm");
		case EPmcOrder::Crouch: return TEXT("Crouch");
		default: return TEXT("?");
		}
	}

	float NumbersFactor(int32 Alive, int32 EnemiesKnown)
	{
		return PmcCommanderLocal::Clamp01(0.5f + 0.25f * static_cast<float>(Alive - EnemiesKnown));
	}

	float ScoreOrder(EPmcOrder Order, const FPmcFighterView& F, const FPmcSquadView& S, const FPmcWeights& W)
	{
		using namespace PmcCommanderLocal;
		if (F.bKnockedDown)
		{
			return 0.f;
		}
		const float Contact = Step(S.bContact);
		const float SeenNow = Step(F.bHasThreat && F.Threat.bSeenNow);
		const float InRange = Step(F.bHasThreat && F.Threat.DistanceCm <= F.WeaponRangeCm);
		switch (Order)
		{
		case EPmcOrder::Hold:
			// The baseline: the fighter keeps shooting from where he is; worth more in cover and in contact.
			return W.Hold * (F.bInCover ? 0.6f + 0.4f * F.CoverQuality : 0.3f) * (S.bContact ? 1.f : 0.5f);
		case EPmcOrder::TakeCover:
			// Exposed and badly covered against a known threat, with somewhere to go.
			return W.TakeCover * Step(F.bCoverAvailable && F.bHasThreat) * (1.f - F.CoverQuality) * (0.4f + 0.6f * Contact) * (0.5f + 0.5f * Step(F.bExposed))
				* Step(!F.bInCover || F.CoverQuality < 0.3f);
		case EPmcOrder::Advance:
			// Bounding overwatch: one man moves while the others hold; worth it with numbers, health, ammo, and a threat out of reach.
			return W.Advance * Step(F.bHasThreat) * (S.bContact ? 1.f : 0.4f) * NumbersFactor(S.Alive, S.EnemiesKnown) * F.HealthFrac
				* Step(F.AmmoFrac > 0.2f) * Step(F.Threat.DistanceCm > F.WeaponRangeCm * W.AdvanceStopRangeFrac) * (S.MatesAdvancing == 0 || S.Alive == 1 ? 1.f : 0.15f);
		case EPmcOrder::Flank:
			// A second axis onto a seen threat while a mate pins it; one flanker at a time, a healthy man, within a sensible band.
			return W.Flank * SeenNow * (S.MatesSuppressing > 0 || S.Alive >= 2 ? 1.f : 0.3f) * (S.MatesFlanking == 0 ? 1.f : 0.2f) * F.HealthFrac
				* Step(F.Threat.DistanceCm >= 600.f && F.Threat.DistanceCm <= 3500.f) * (F.bExposed ? 0.7f : 1.f) * Step(F.AmmoFrac > 0.2f);
		case EPmcOrder::Suppress:
			// Pin a seen threat so a mate can move; needs ammo and a reason (a mate moving), one suppressor at a time.
			return W.Suppress * SeenNow * InRange * Step(F.AmmoFrac >= W.SuppressAmmoMin) * (S.MatesFlanking + S.MatesAdvancing > 0 ? 1.f : 0.5f)
				* (S.MatesSuppressing == 0 ? 1.f : 0.4f);
		case EPmcOrder::FallBack:
			// Losing: badly wounded or outnumbered in contact.
			return W.FallBack * Contact * Step(F.bHasThreat) * Clamp01(Step(F.HealthFrac < W.FallBackBelow) * (1.f - F.HealthFrac) + Step(S.Alive - S.EnemiesKnown <= -1) * 0.6f);
		case EPmcOrder::Heal:
			// A wounded mate within reach, a medkit in the kit, and nobody looking (seen threats make it a last resort).
			return W.Heal * Step(F.bHasMedkit && F.WoundedMateDistanceCm >= 0.f && F.WoundedMateHealthFrac < W.HealBelow) * Clamp01(1.f - F.WoundedMateHealthFrac)
				* (S.EnemiesSeenNow == 0 ? 1.f : 0.3f) * (S.MatesHealing == 0 ? 1.f : 0.2f);
		case EPmcOrder::Relocate:
			// Spotted at this spot for too long: move to another cover.
			return W.Relocate * Contact * Step(F.bExposed && F.SecondsExposedHere >= W.RelocateAfterSeconds && F.bCoverAvailable);
		case EPmcOrder::Ambush:
			// No contact yet in this fight: hold in cover, crouched, and let them come.
			return W.Ambush * Step(!S.bEverContact) * (F.bInCover ? 1.f : 0.6f);
		case EPmcOrder::Reload:
			// A low clip with rounds in reserve; best behind cover or out of contact.
			return W.Reload * Step(F.ClipFrac < 0.3f && F.bHasReserve) * (F.bInCover || S.EnemiesSeenNow == 0 ? 1.f : 0.5f);
		case EPmcOrder::Sidearm:
			// An empty rifle with the enemy at point-blank range: the pistol.
			return W.Sidearm * Step(F.bHasSidearm && F.ClipFrac <= 0.f && F.bHasThreat && F.Threat.DistanceCm < 400.f && F.Threat.bSeenNow);
		case EPmcOrder::Crouch:
			// In contact, standing still in the open: get low.
			return W.Crouch * Contact * Step(!F.bMoving && !F.bInCover && F.Stance == EOperativeStance::Standing);
		default:
			return 0.f;
		}
	}

	TArray<FPmcOrderScore> ScoreAll(const FPmcFighterView& Fighter, const FPmcSquadView& Squad, const FPmcWeights& Weights)
	{
		TArray<FPmcOrderScore> Out;
		for (int32 Index = 0; Index < static_cast<int32>(EPmcOrder::Count); ++Index)
		{
			const EPmcOrder Order = static_cast<EPmcOrder>(Index);
			Out.Add({ Order, ScoreOrder(Order, Fighter, Squad, Weights) });
		}
		Out.StableSort([](const FPmcOrderScore& A, const FPmcOrderScore& B) { return A.Score > B.Score; });
		return Out;
	}

	EPmcOrder PickWithMargin(const TArray<FPmcOrderScore>& Scored, EPmcOrder Current, bool bCurrentRunning, float Margin)
	{
		if (Scored.IsEmpty() || Scored[0].Score <= 0.f)
		{
			return EPmcOrder::Hold;
		}
		if (bCurrentRunning)
		{
			for (const FPmcOrderScore& Entry : Scored)
			{
				if (Entry.Order == Current)
				{
					return Entry.Score > 0.f && Entry.Score >= Scored[0].Score - Margin ? Current : Scored[0].Order;
				}
			}
		}
		return Scored[0].Order;
	}

	EPmcOrder Pick(const TArray<FPmcOrderScore>& Scored, EPmcOrder Current, bool bCurrentRunning, const FPmcWeights& Weights)
	{
		return PickWithMargin(Scored, Current, bCurrentRunning, Weights.SwitchMargin);
	}

	bool IsMoveOrder(EPmcOrder Order)
	{
		return Order == EPmcOrder::Advance || Order == EPmcOrder::Flank || Order == EPmcOrder::FallBack || Order == EPmcOrder::Relocate || Order == EPmcOrder::TakeCover;
	}

	FVector FlankPoint(const FVector& Threat, const FVector& SquadCentre, const FVector& Fighter, const FPmcWeights& Weights)
	{
		FVector Axis = (SquadCentre - Threat).GetSafeNormal2D();
		if (Axis.IsNearlyZero())
		{
			Axis = FVector::ForwardVector;
		}
		const FVector Side = FVector::CrossProduct(FVector::UpVector, Axis).GetSafeNormal2D();
		// The side away from the fighter: he comes at the threat from the other flank than the one he stands on.
		const float Sign = FVector::DotProduct(Fighter - Threat, Side) >= 0.f ? -1.f : 1.f;
		const FVector Direction = (Axis * FMath::Cos(FMath::DegreesToRadians(Weights.FlankAngleDeg)) + Side * Sign * FMath::Sin(FMath::DegreesToRadians(Weights.FlankAngleDeg))).GetSafeNormal2D();
		return FVector(Threat.X, Threat.Y, Threat.Z) + Direction * Weights.FlankDistanceCm;
	}

	FVector AdvancePoint(const FVector& Fighter, const FVector& Threat, float WeaponRangeCm, const FPmcWeights& Weights)
	{
		const float Distance = FVector::Dist2D(Fighter, Threat);
		const float Stop = WeaponRangeCm * Weights.AdvanceStopRangeFrac;
		if (Distance <= Stop)
		{
			return Fighter;
		}
		const FVector Direction = (Threat - Fighter).GetSafeNormal2D();
		return Fighter + Direction * (Distance - Stop);
	}

	FVector FallBackPoint(const FVector& Fighter, const FVector& Threat, float DistanceCm)
	{
		FVector Away = (Fighter - Threat).GetSafeNormal2D();
		if (Away.IsNearlyZero())
		{
			Away = FVector::BackwardVector;
		}
		return Fighter + Away * DistanceCm;
	}

	float CoverQualityAgainst(const FVector& SlotLocation, const FVector& WallNormal, const FVector& Threat)
	{
		const FVector ToThreat = (Threat - SlotLocation).GetSafeNormal2D();
		return PmcCommanderLocal::Clamp01(FVector::DotProduct(-WallNormal.GetSafeNormal2D(), ToThreat));
	}

	float ApproachAngleDeg(const FVector& Threat, const FVector& A, const FVector& B)
	{
		const FVector DirA = (A - Threat).GetSafeNormal2D();
		const FVector DirB = (B - Threat).GetSafeNormal2D();
		if (DirA.IsNearlyZero() || DirB.IsNearlyZero())
		{
			return 0.f;
		}
		return FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(DirA, DirB), -1.f, 1.f)));
	}

	FPmcWeights FromJson(const FJsonObject& Json, const FPmcWeights& Base)
	{
		using namespace PmcCommanderLocal;
		FPmcWeights Out = Base;
		FString Name;
		if (Json.TryGetStringField(TEXT("name"), Name))
		{
			Out.Name = Name;
		}
		ReadNumber(Json, TEXT("hold"), Out.Hold);
		ReadNumber(Json, TEXT("take_cover"), Out.TakeCover);
		ReadNumber(Json, TEXT("advance"), Out.Advance);
		ReadNumber(Json, TEXT("flank"), Out.Flank);
		ReadNumber(Json, TEXT("suppress"), Out.Suppress);
		ReadNumber(Json, TEXT("fall_back"), Out.FallBack);
		ReadNumber(Json, TEXT("heal"), Out.Heal);
		ReadNumber(Json, TEXT("relocate"), Out.Relocate);
		ReadNumber(Json, TEXT("ambush"), Out.Ambush);
		ReadNumber(Json, TEXT("reload"), Out.Reload);
		ReadNumber(Json, TEXT("sidearm"), Out.Sidearm);
		ReadNumber(Json, TEXT("crouch"), Out.Crouch);
		ReadNumber(Json, TEXT("heal_below"), Out.HealBelow);
		ReadNumber(Json, TEXT("fall_back_below"), Out.FallBackBelow);
		ReadNumber(Json, TEXT("suppress_ammo_min"), Out.SuppressAmmoMin);
		ReadNumber(Json, TEXT("advance_stop_range_frac"), Out.AdvanceStopRangeFrac);
		ReadNumber(Json, TEXT("flank_distance_cm"), Out.FlankDistanceCm);
		ReadNumber(Json, TEXT("flank_angle_deg"), Out.FlankAngleDeg);
		ReadNumber(Json, TEXT("relocate_after_seconds"), Out.RelocateAfterSeconds);
		ReadNumber(Json, TEXT("contact_memory_seconds"), Out.ContactMemorySeconds);
		ReadNumber(Json, TEXT("decision_interval"), Out.DecisionInterval);
		ReadNumber(Json, TEXT("switch_margin"), Out.SwitchMargin);
		ReadNumber(Json, TEXT("move_switch_margin"), Out.MoveSwitchMargin);
		ReadNumber(Json, TEXT("commit_seconds"), Out.CommitSeconds);
		Out.DecisionInterval = FMath::Max(0.1f, Out.DecisionInterval);
		return Out;
	}

	FString DefaultWeightsPath()
	{
		return FPaths::ProjectContentDir() / TEXT("Data/AI/pmc_commander.json");
	}

	bool LoadWeightsFile(const FString& PathOrName, FPmcWeights& Out)
	{
		FString Path = PathOrName.IsEmpty() ? DefaultWeightsPath() : PathOrName;
		if (!FPaths::FileExists(Path))
		{
			Path = FPaths::ProjectContentDir() / TEXT("Data/AI") / PathOrName;
		}
		FString Text;
		TSharedPtr<FJsonObject> Json;
		if (!FPaths::FileExists(Path) || !FFileHelper::LoadFileToString(Text, *Path)
			|| !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Json) || !Json.IsValid())
		{
			Out = FPmcWeights();
			return false;
		}
		Out = FromJson(*Json);
		if (Out.Name == TEXT("default"))
		{
			Out.Name = FPaths::GetBaseFilename(Path);
		}
		return true;
	}
}
