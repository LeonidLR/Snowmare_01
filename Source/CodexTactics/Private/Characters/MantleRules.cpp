#include "Characters/MantleRules.h"

#include "CodexTactics.h"
#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace MantleRules
{
	const FName MarkedEdgeTag(TEXT("CodexMantleEdge"));
	const FName MantleTag(TEXT("Mantle"));
	const FName NoMantleTag(TEXT("NoMantle"));
}

namespace MantleRulesInternal
{
	/** One "params" key and its member. */
	struct FMantleParamKey
	{
		const TCHAR* Key;
		float FMantleConfig::* Member;
	};

	static const FMantleParamKey MantleParamKeys[] = {
		{ TEXT("ceiling_cm"), &FMantleConfig::CeilingCm },
		{ TEXT("top_min_depth_cm"), &FMantleConfig::TopMinDepthCm },
		{ TEXT("over_max_depth_cm"), &FMantleConfig::OverMaxDepthCm },
		{ TEXT("top_inset_cm"), &FMantleConfig::TopInsetCm },
		{ TEXT("min_face_gap_cm"), &FMantleConfig::MinFaceGapCm },
		{ TEXT("over_start_cm"), &FMantleConfig::OverStartCm },
		{ TEXT("over_max_step_cm"), &FMantleConfig::OverMaxStepCm },
		{ TEXT("headroom_margin_cm"), &FMantleConfig::HeadroomMarginCm },
		{ TEXT("max_vertical_warp"), &FMantleConfig::MaxVerticalWarp },
		{ TEXT("max_forward_warp"), &FMantleConfig::MaxForwardWarp },
		{ TEXT("standing_slack_cm"), &FMantleConfig::StandingSlackCm },
		{ TEXT("run_speed_fraction"), &FMantleConfig::RunSpeedFraction },
		{ TEXT("cooldown_s"), &FMantleConfig::CooldownSeconds },
		{ TEXT("timeout_pad_s"), &FMantleConfig::TimeoutPadSeconds },
		{ TEXT("blend_in_s"), &FMantleConfig::BlendInSeconds },
		{ TEXT("blend_out_s"), &FMantleConfig::BlendOutSeconds },
		{ TEXT("heavy_hit_damage"), &FMantleConfig::HeavyHitDamage },
		{ TEXT("landing_snap_warn_cm"), &FMantleConfig::LandingSnapWarnCm },
		{ TEXT("links.spacing_cm"), &FMantleConfig::LinkSpacingCm },
		{ TEXT("links.min_face_cm"), &FMantleConfig::LinkMinFaceCm },
		{ TEXT("links.max_per_face"), &FMantleConfig::LinkMaxPerFace },
		{ TEXT("links.entering_cost_cm"), &FMantleConfig::LinkEnteringCostCm },
		{ TEXT("links.down_below_cm"), &FMantleConfig::LinkDownBelowCm },
		{ TEXT("links.max_total"), &FMantleConfig::LinkMaxTotal },
		{ TEXT("order.max_distance_cm"), &FMantleConfig::OrderMaxDistanceCm },
		{ TEXT("order.face_inset_cm"), &FMantleConfig::OrderFaceInsetCm },
		{ TEXT("reserve.radius_cm"), &FMantleConfig::ReserveRadiusCm },
		{ TEXT("align.sample_hz"), &FMantleConfig::AlignSampleHz },
		{ TEXT("align.hand_edge_tolerance_cm"), &FMantleConfig::AlignHandEdgeToleranceCm },
		{ TEXT("align.pop_tolerance_cm"), &FMantleConfig::AlignPopToleranceCm },
		{ TEXT("align.freeze_limit_s"), &FMantleConfig::AlignFreezeLimitSeconds },
		{ TEXT("align.still_speed_cm_s"), &FMantleConfig::AlignStillSpeedCmPerSecond },
		{ TEXT("align.plant_speed_cm_s"), &FMantleConfig::AlignPlantSpeedCmPerSecond },
		{ TEXT("align.plant_reach_cm"), &FMantleConfig::AlignPlantReachCm },
		{ TEXT("align.plant_min_rise_fraction"), &FMantleConfig::AlignPlantMinRiseFraction },
		{ TEXT("align.plant_hold_s"), &FMantleConfig::AlignPlantHoldSeconds },
		{ TEXT("align.exit_root_cm"), &FMantleConfig::AlignExitRootCm },
		{ TEXT("align.grip_forward_cm"), &FMantleConfig::AlignGripForwardCm },
		{ TEXT("align.grip_up_cm"), &FMantleConfig::AlignGripUpCm },
		{ TEXT("align.exit_pose_cm"), &FMantleConfig::AlignExitPoseCm },
		{ TEXT("align.guard_gap_cm"), &FMantleConfig::AlignGuardGapCm },
		{ TEXT("align.guard_max_push_cm"), &FMantleConfig::AlignGuardMaxPushCm },
		{ TEXT("align.min_body_gap_cm"), &FMantleConfig::AlignMinBodyGapCm },
		{ TEXT("align.gap_above_margin_cm"), &FMantleConfig::AlignGapAboveMarginCm },
		{ TEXT("align.settle_pelvis_cm"), &FMantleConfig::AlignSettlePelvisCm },
		{ TEXT("align.handoff_limit_s"), &FMantleConfig::AlignHandoffLimitSeconds },
		{ TEXT("descent.arrive_limit_s"), &FMantleConfig::DescentArriveLimitSeconds },
		{ TEXT("descent.arrive_radius_cm"), &FMantleConfig::DescentArriveRadiusCm },
		{ TEXT("descent.max_speed_cm_s"), &FMantleConfig::DescentMaxSpeedCmPerSecond },
		{ TEXT("descent.land_out_cm"), &FMantleConfig::DescentLandOutCm },
		{ TEXT("descent.min_drop_cm"), &FMantleConfig::DescentMinDropCm },
		{ TEXT("descent.max_drop_cm"), &FMantleConfig::DescentMaxDropCm },
		{ TEXT("descent.stand_up_max_s"), &FMantleConfig::DescentStandUpMaxSeconds },
		{ TEXT("descent.walk_timeout_s"), &FMantleConfig::DescentWalkTimeoutSeconds },
		{ TEXT("descent.prefer_margin_cm"), &FMantleConfig::DescentPreferMarginCm },
	};

	static FMantleConfig& MantleConfigInForce()
	{
		static FMantleConfig Config = MantleRules::MakeDefaultConfig();
		return Config;
	}

	static bool bMantleConfigLoaded = false;

	static FMantleHeightClass MakeMantleClass(const TCHAR* Id, float Nominal, float Tolerance, EMantleDestination Destination, bool bMarkedOnly = false,
		bool bVault = false)
	{
		FMantleHeightClass Class;
		Class.Id = FName(Id);
		Class.NominalCm = Nominal;
		Class.ToleranceCm = Tolerance;
		Class.Destination = Destination;
		Class.bMarkedEdgeOnly = bMarkedOnly;
		Class.bHandledByVault = bVault;
		return Class;
	}

	static FMantleClip MakeMantleClip(const TCHAR* ClassId, EMantleGait Gait, EMantleBody Body, const TCHAR* Name, float Length, float Rise, float Forward,
		float Start = 0.f)
	{
		FMantleClip Clip;
		Clip.ClassId = FName(ClassId);
		Clip.Gait = Gait;
		Clip.Body = Body;
		const TCHAR* Folder = Body == EMantleBody::Male ? TEXT("Male") : TEXT("Female");
		Clip.Path = FString::Printf(TEXT("/Game/Characters/Animations/Mantle/%s/%s.%s"), Folder, Name, Name);
		Clip.LengthSeconds = Length;
		Clip.RiseCm = Rise;
		Clip.ForwardCm = Forward;
		Clip.StartCm = Start;
		return Clip;
	}

	static bool ParseMantleEnum(const FString& Text, const TCHAR* A, const TCHAR* B, bool& bOutSecond)
	{
		if (Text.Equals(A, ESearchCase::IgnoreCase)) { bOutSecond = false; return true; }
		if (Text.Equals(B, ESearchCase::IgnoreCase)) { bOutSecond = true; return true; }
		return false;
	}

	static TSharedPtr<FJsonObject> ParseMantleJsonObject(const FString& JsonText, FString* OutError)
	{
		TSharedPtr<FJsonObject> Root;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonText);
		if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
		{
			if (OutError) { *OutError = TEXT("not valid JSON"); }
			return nullptr;
		}
		return Root;
	}
}

FMantleConfig MantleRules::MakeDefaultConfig()
{
	using namespace MantleRulesInternal;
	FMantleConfig Config;
	// obstacle_heights.json (approved targets, team-chat #465) + mantle.json's per-class action.
	Config.Classes.Add(MakeMantleClass(TEXT("low_vault"), 95.f, 5.f, EMantleDestination::Over, false, true));
	Config.Classes.Add(MakeMantleClass(TEXT("low_mantle"), 85.f, 10.f, EMantleDestination::Top));
	Config.Classes.Add(MakeMantleClass(TEXT("high_mantle"), 140.f, 10.f, EMantleDestination::Top));
	Config.Classes.Add(MakeMantleClass(TEXT("tall_ledge"), 230.f, 10.f, EMantleDestination::Top));
	Config.Classes.Add(MakeMantleClass(TEXT("very_tall"), 310.f, 10.f, EMantleDestination::Top, true));
	Config.Classes.Add(MakeMantleClass(TEXT("high_fence_over"), 135.f, 5.f, EMantleDestination::Over));
	// The retargeted copies' root tracks (Saved/Logs/setup_mantle_animation.txt, 2026-11-15): length s, rise cm, forward cm.
	constexpr EMantleGait S = EMantleGait::Standing;
	constexpr EMantleGait R = EMantleGait::Running;
	constexpr EMantleBody M = EMantleBody::Male;
	constexpr EMantleBody F = EMantleBody::Female;
	Config.Clips = {
		MakeMantleClip(TEXT("low_mantle"), S, M, TEXT("Low_Climb"), 1.017f, 83.6f, 118.9f),
		MakeMantleClip(TEXT("low_mantle"), R, M, TEXT("RunLowClimb"), 0.9f, 98.5f, 157.8f),
		MakeMantleClip(TEXT("high_mantle"), S, M, TEXT("HighClimb"), 1.033f, 142.6f, 171.1f),
		MakeMantleClip(TEXT("high_mantle"), R, M, TEXT("RunHighClimb"), 1.533f, 154.3f, 431.4f),
		MakeMantleClip(TEXT("tall_ledge"), S, M, TEXT("TallLedge_ClimbUpNEW"), 5.2f, 250.4f, 103.6f),
		MakeMantleClip(TEXT("tall_ledge"), S, M, TEXT("Standing_JumpClimbUpNEW"), 5.7f, 248.4f, 93.9f),
		MakeMantleClip(TEXT("tall_ledge"), R, M, TEXT("RunJumpClimbNEW"), 2.2f, 258.8f, 493.5f),
		MakeMantleClip(TEXT("very_tall"), S, M, TEXT("JumpClimb"), 6.533f, 334.4f, 69.1f),
		MakeMantleClip(TEXT("very_tall"), R, M, TEXT("RunJumpClimb"), 3.083f, 334.1f, 432.2f),
		MakeMantleClip(TEXT("high_fence_over"), S, M, TEXT("HighFenceVault"), 2.1f, 148.7f, 344.2f, 110.f),
		MakeMantleClip(TEXT("low_mantle"), S, F, TEXT("Low_Climb"), 1.017f, 80.1f, 113.9f),
		MakeMantleClip(TEXT("low_mantle"), R, F, TEXT("RunLowClimb"), 0.9f, 94.4f, 151.3f),
		MakeMantleClip(TEXT("high_mantle"), S, F, TEXT("HighClimb"), 1.033f, 136.7f, 164.f),
		MakeMantleClip(TEXT("high_mantle"), R, F, TEXT("RunHighClimb"), 1.533f, 147.9f, 413.5f),
		MakeMantleClip(TEXT("tall_ledge"), S, F, TEXT("TallLedge_ClimbUpNEW"), 5.2f, 240.f, 99.3f),
		MakeMantleClip(TEXT("tall_ledge"), S, F, TEXT("Standing_JumpClimbUpNEW"), 5.7f, 238.1f, 90.f),
		MakeMantleClip(TEXT("tall_ledge"), R, F, TEXT("RunJumpClimbNEW"), 2.2f, 248.1f, 473.f),
		MakeMantleClip(TEXT("very_tall"), S, F, TEXT("JumpClimb"), 6.533f, 320.5f, 66.2f),
		MakeMantleClip(TEXT("very_tall"), R, F, TEXT("RunJumpClimb"), 3.083f, 320.2f, 414.3f),
		MakeMantleClip(TEXT("high_fence_over"), S, F, TEXT("HighFenceVault"), 2.1f, 142.5f, 329.9f, 110.f),
	};
	return Config;
}

bool MantleRules::ApplyHeightsJson(const FString& JsonText, FMantleConfig& InOut, FString* OutError)
{
	const TSharedPtr<FJsonObject> Root = MantleRulesInternal::ParseMantleJsonObject(JsonText, OutError);
	const TSharedPtr<FJsonObject>* Classes = nullptr;
	if (!Root.IsValid() || !Root->TryGetObjectField(TEXT("classes"), Classes) || !Classes)
	{
		if (OutError && Root.IsValid()) { *OutError = TEXT("no \"classes\" object"); }
		return false;
	}
	for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*Classes)->Values)
	{
		const TSharedPtr<FJsonObject>* Class = nullptr;
		if (!Pair.Value.IsValid() || !Pair.Value->TryGetObject(Class) || !Class)
		{
			continue;
		}
		if (Pair.Key == TEXT("wall_no_traverse"))
		{
			double MinCm = 0.0;
			if ((*Class)->TryGetNumberField(TEXT("min_cm"), MinCm))
			{
				InOut.CeilingCm = static_cast<float>(MinCm);
			}
			continue;
		}
		const int32 Index = FindClass(InOut, FName(*Pair.Key));
		if (Index == INDEX_NONE)
		{
			continue; // step and any class the mantle has no action for
		}
		double Nominal = 0.0;
		double Tolerance = 0.0;
		if ((*Class)->TryGetNumberField(TEXT("nominal_cm"), Nominal))
		{
			InOut.Classes[Index].NominalCm = static_cast<float>(Nominal);
		}
		if ((*Class)->TryGetNumberField(TEXT("tolerance_cm"), Tolerance))
		{
			InOut.Classes[Index].ToleranceCm = static_cast<float>(Tolerance);
		}
	}
	return true;
}

bool MantleRules::ApplyMantleJson(const FString& JsonText, FMantleConfig& InOut, TArray<FString>* OutUnknown, FString* OutError)
{
	using namespace MantleRulesInternal;
	const TSharedPtr<FJsonObject> Root = ParseMantleJsonObject(JsonText, OutError);
	const TSharedPtr<FJsonObject>* Params = nullptr;
	if (!Root.IsValid() || !Root->TryGetObjectField(TEXT("params"), Params) || !Params)
	{
		if (OutError && Root.IsValid()) { *OutError = TEXT("no \"params\" object"); }
		return false;
	}
	for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*Params)->Values)
	{
		double Number = 0.0;
		const FMantleParamKey* Found = nullptr;
		for (const FMantleParamKey& Key : MantleParamKeys)
		{
			if (Pair.Key == Key.Key)
			{
				Found = &Key;
				break;
			}
		}
		if (Found && Pair.Value.IsValid() && Pair.Value->TryGetNumber(Number))
		{
			InOut.*(Found->Member) = static_cast<float>(Number);
		}
		else if (OutUnknown)
		{
			OutUnknown->Add(Pair.Key);
		}
	}
	const TSharedPtr<FJsonObject>* Classes = nullptr;
	if (Root->TryGetObjectField(TEXT("classes"), Classes) && Classes)
	{
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*Classes)->Values)
		{
			const TSharedPtr<FJsonObject>* Class = nullptr;
			if (!Pair.Value.IsValid() || !Pair.Value->TryGetObject(Class) || !Class)
			{
				continue;
			}
			int32 Index = FindClass(InOut, FName(*Pair.Key));
			if (Index == INDEX_NONE)
			{
				FMantleHeightClass Added;
				Added.Id = FName(*Pair.Key);
				Index = InOut.Classes.Add(Added);
			}
			FMantleHeightClass& Target = InOut.Classes[Index];
			FString Destination;
			bool bOver = false;
			if ((*Class)->TryGetStringField(TEXT("destination"), Destination) && ParseMantleEnum(Destination, TEXT("top"), TEXT("over"), bOver))
			{
				Target.Destination = bOver ? EMantleDestination::Over : EMantleDestination::Top;
			}
			(*Class)->TryGetBoolField(TEXT("enabled"), Target.bEnabled);
			(*Class)->TryGetBoolField(TEXT("marked_edge_only"), Target.bMarkedEdgeOnly);
			FString HandledBy;
			if ((*Class)->TryGetStringField(TEXT("handled_by"), HandledBy))
			{
				Target.bHandledByVault = HandledBy.Equals(TEXT("vault"), ESearchCase::IgnoreCase);
			}
		}
	}
	const TArray<TSharedPtr<FJsonValue>>* Clips = nullptr;
	if (Root->TryGetArrayField(TEXT("clips"), Clips) && Clips)
	{
		TArray<FMantleClip> Parsed;
		for (const TSharedPtr<FJsonValue>& Value : *Clips)
		{
			const TSharedPtr<FJsonObject>* Object = nullptr;
			if (!Value.IsValid() || !Value->TryGetObject(Object) || !Object)
			{
				continue;
			}
			FMantleClip Clip;
			FString ClassId;
			FString Gait;
			FString Body;
			bool bSecond = false;
			if (!(*Object)->TryGetStringField(TEXT("class"), ClassId) || !(*Object)->TryGetStringField(TEXT("path"), Clip.Path))
			{
				continue;
			}
			Clip.ClassId = FName(*ClassId);
			if ((*Object)->TryGetStringField(TEXT("gait"), Gait) && ParseMantleEnum(Gait, TEXT("standing"), TEXT("running"), bSecond))
			{
				Clip.Gait = bSecond ? EMantleGait::Running : EMantleGait::Standing;
			}
			if ((*Object)->TryGetStringField(TEXT("body"), Body) && ParseMantleEnum(Body, TEXT("male"), TEXT("female"), bSecond))
			{
				Clip.Body = bSecond ? EMantleBody::Female : EMantleBody::Male;
			}
			double Number = 0.0;
			if ((*Object)->TryGetNumberField(TEXT("length_s"), Number)) { Clip.LengthSeconds = static_cast<float>(Number); }
			if ((*Object)->TryGetNumberField(TEXT("rise_cm"), Number)) { Clip.RiseCm = static_cast<float>(Number); }
			if ((*Object)->TryGetNumberField(TEXT("forward_cm"), Number)) { Clip.ForwardCm = static_cast<float>(Number); }
			if ((*Object)->TryGetNumberField(TEXT("start_cm"), Number)) { Clip.StartCm = static_cast<float>(Number); }
			Parsed.Add(MoveTemp(Clip));
		}
		InOut.Clips = MoveTemp(Parsed);
	}
	return true;
}

TArray<FString> MantleRules::KnownKeys()
{
	TArray<FString> Keys;
	for (const MantleRulesInternal::FMantleParamKey& Key : MantleRulesInternal::MantleParamKeys)
	{
		Keys.Add(Key.Key);
	}
	return Keys;
}

bool MantleRules::GetValue(const FMantleConfig& Config, const FString& Key, float& OutValue)
{
	for (const MantleRulesInternal::FMantleParamKey& Each : MantleRulesInternal::MantleParamKeys)
	{
		if (Key == Each.Key)
		{
			OutValue = Config.*(Each.Member);
			return true;
		}
	}
	return false;
}

FString MantleRules::GetDefaultPath()
{
	return FPaths::ProjectContentDir() / TEXT("Data/Movement/mantle.json");
}

FString MantleRules::GetHeightsPath()
{
	return FPaths::ProjectContentDir() / TEXT("Data/LevelGen/obstacle_heights.json");
}

const FMantleConfig& MantleRules::GetConfig()
{
	if (!MantleRulesInternal::bMantleConfigLoaded)
	{
		ReloadConfig();
	}
	return MantleRulesInternal::MantleConfigInForce();
}

void MantleRules::SetConfig(const FMantleConfig& Config)
{
	MantleRulesInternal::bMantleConfigLoaded = true;
	MantleRulesInternal::MantleConfigInForce() = Config;
}

bool MantleRules::ReloadConfig()
{
	MantleRulesInternal::bMantleConfigLoaded = true;
	FMantleConfig Config = MakeDefaultConfig();
	bool bOk = true;
	FString Text;
	FString Error;
	if (FFileHelper::LoadFileToString(Text, *GetDefaultPath()))
	{
		TArray<FString> Unknown;
		if (!ApplyMantleJson(Text, Config, &Unknown, &Error))
		{
			UE_LOG(LogCodexTactics, Warning, TEXT("[Mantle] %s unreadable (%s) - the code defaults are used"), *GetDefaultPath(), *Error);
			bOk = false;
		}
		for (const FString& Key : Unknown)
		{
			UE_LOG(LogCodexTactics, Warning, TEXT("[Mantle] %s: unknown param \"%s\""), *GetDefaultPath(), *Key);
		}
	}
	else
	{
		UE_LOG(LogCodexTactics, Warning, TEXT("[Mantle] %s missing - the code defaults are used"), *GetDefaultPath());
		bOk = false;
	}
	if (FFileHelper::LoadFileToString(Text, *GetHeightsPath()))
	{
		if (!ApplyHeightsJson(Text, Config, &Error))
		{
			UE_LOG(LogCodexTactics, Warning, TEXT("[Mantle] %s unreadable (%s) - the default height classes are used"), *GetHeightsPath(), *Error);
			bOk = false;
		}
	}
	else
	{
		UE_LOG(LogCodexTactics, Warning, TEXT("[Mantle] %s missing - the default height classes are used"), *GetHeightsPath());
		bOk = false;
	}
	MantleRulesInternal::MantleConfigInForce() = MoveTemp(Config);
	return bOk;
}

int32 MantleRules::FindClass(const FMantleConfig& Config, FName Id)
{
	return Config.Classes.IndexOfByPredicate([Id](const FMantleHeightClass& Class) { return Class.Id == Id; });
}

const TCHAR* MantleRules::RefusalName(EMantleRefusal Refusal)
{
	switch (Refusal)
	{
	case EMantleRefusal::None: return TEXT("none");
	case EMantleRefusal::NoObstacle: return TEXT("no obstacle");
	case EMantleRefusal::NotEligible: return TEXT("not a mantle target");
	case EMantleRefusal::TooLow: return TEXT("too low");
	case EMantleRefusal::VaultInstead: return TEXT("the vault takes it");
	case EMantleRefusal::BetweenClasses: return TEXT("between height classes");
	case EMantleRefusal::Wall: return TEXT("a wall (above the traversal ceiling)");
	case EMantleRefusal::ClassDisabled: return TEXT("class disabled");
	case EMantleRefusal::NotMarked: return TEXT("not a marked mantle edge");
	case EMantleRefusal::DepthMismatch: return TEXT("neither a top to stand on nor thin enough to climb over");
	case EMantleRefusal::NoHeadroom: return TEXT("no headroom on the landing");
	case EMantleRefusal::NoLanding: return TEXT("no free landing");
	case EMantleRefusal::LandingStep: return TEXT("the far side's ground is too far below / above");
	case EMantleRefusal::MissingClip: return TEXT("no clip for this body");
	case EMantleRefusal::WarpTooLarge: return TEXT("the clip would need too much warping");
	default: return TEXT("?");
	}
}

bool MantleRules::IsEligibleTags(const TArray<FName>& Tags, bool& OutMarked)
{
	static const FName EligibleKinds[] = { FName(TEXT("crate")), FName(TEXT("wreck")), FName(TEXT("boiler")), FName(TEXT("cut_wall")),
		FName(TEXT("fence")) };
	OutMarked = Tags.Contains(MarkedEdgeTag);
	if (Tags.Contains(NoMantleTag))
	{
		return false;
	}
	if (OutMarked || Tags.Contains(MantleTag))
	{
		return true;
	}
	for (const FName& Kind : EligibleKinds)
	{
		if (Tags.Contains(Kind))
		{
			return true;
		}
	}
	return false;
}

TOptional<EMantleDestination> MantleRules::DestinationForDepth(const FMantleConfig& Config, float DepthCm)
{
	if (DepthCm >= Config.TopMinDepthCm)
	{
		return EMantleDestination::Top;
	}
	if (DepthCm > 0.f && DepthCm <= Config.OverMaxDepthCm)
	{
		return EMantleDestination::Over;
	}
	return TOptional<EMantleDestination>();
}

int32 MantleRules::ClassifyHeight(const FMantleConfig& Config, float HeightCm, EMantleDestination Destination, EMantleRefusal& OutRefusal)
{
	if (HeightCm > Config.CeilingCm)
	{
		OutRefusal = EMantleRefusal::Wall;
		return INDEX_NONE;
	}
	int32 Best = INDEX_NONE;
	float BestOff = TNumericLimits<float>::Max();
	float LowestMin = TNumericLimits<float>::Max();
	for (int32 Index = 0; Index < Config.Classes.Num(); ++Index)
	{
		const FMantleHeightClass& Class = Config.Classes[Index];
		if (Class.Destination != Destination)
		{
			continue;
		}
		LowestMin = FMath::Min(LowestMin, Class.MinCm());
		if (HeightCm >= Class.MinCm() && HeightCm <= Class.MaxCm())
		{
			const float Off = FMath::Abs(HeightCm - Class.NominalCm);
			if (Off < BestOff)
			{
				BestOff = Off;
				Best = Index;
			}
		}
	}
	if (Best != INDEX_NONE)
	{
		OutRefusal = EMantleRefusal::None;
		return Best;
	}
	OutRefusal = HeightCm < LowestMin ? EMantleRefusal::TooLow : EMantleRefusal::BetweenClasses;
	return INDEX_NONE;
}

float MantleRules::ClipStartCm(const FMantleConfig& Config, const FMantleClip& Clip)
{
	const int32 ClassIndex = FindClass(Config, Clip.ClassId);
	const bool bOver = Config.Classes.IsValidIndex(ClassIndex) && Config.Classes[ClassIndex].Destination == EMantleDestination::Over;
	if (bOver)
	{
		return FMath::Max(Config.MinFaceGapCm, Clip.StartCm > 0.f ? Clip.StartCm : Config.OverStartCm);
	}
	return FMath::Max(Config.MinFaceGapCm, Clip.ForwardCm - Config.TopInsetCm);
}

float MantleRules::ForwardWarpFor(const FMantleConfig& Config, const FMantleClip& Clip, float FaceDistanceCm)
{
	const int32 ClassIndex = FindClass(Config, Clip.ClassId);
	const bool bOver = Config.Classes.IsValidIndex(ClassIndex) && Config.Classes[ClassIndex].Destination == EMantleDestination::Over;
	if (bOver || Clip.ForwardCm <= KINDA_SMALL_NUMBER)
	{
		return 1.f;
	}
	return (FMath::Max(0.f, FaceDistanceCm) + Config.TopInsetCm) / Clip.ForwardCm;
}

int32 MantleRules::ChooseClip(const FMantleConfig& Config, FName ClassId, EMantleBody Body, bool bRunning, float FaceDistanceCm, int32 Rotation,
	TFunctionRef<bool(int32)> IsAvailable, EMantleGait& OutGait, bool& OutStopFirst)
{
	OutStopFirst = false;
	auto Candidates = [&Config, ClassId, Body, &IsAvailable](EMantleGait Gait)
	{
		TArray<int32> Found;
		for (int32 Index = 0; Index < Config.Clips.Num(); ++Index)
		{
			const FMantleClip& Clip = Config.Clips[Index];
			if (Clip.ClassId == ClassId && Clip.Body == Body && Clip.Gait == Gait && IsAvailable(Index))
			{
				Found.Add(Index);
			}
		}
		return Found;
	};
	auto Pick = [Rotation](const TArray<int32>& Found)
	{
		return Found.IsEmpty() ? INDEX_NONE : Found[((Rotation % Found.Num()) + Found.Num()) % Found.Num()];
	};
	if (bRunning)
	{
		// The run clip only when its run-up fits: the face no nearer than the clip's warp window allows (never stretched to start early).
		TArray<int32> Runs = Candidates(EMantleGait::Running);
		Runs.RemoveAll([&Config, FaceDistanceCm](int32 Index)
		{
			const float Warp = ForwardWarpFor(Config, Config.Clips[Index], FaceDistanceCm);
			return Warp < 1.f - Config.MaxForwardWarp || Warp > 1.f + Config.MaxForwardWarp;
		});
		if (const int32 Run = Pick(Runs); Run != INDEX_NONE)
		{
			OutGait = EMantleGait::Running;
			return Run;
		}
		OutStopFirst = true; // designer Q2 = A: not enough run-up -> stop, the standing clip
	}
	OutGait = EMantleGait::Standing;
	return Pick(Candidates(EMantleGait::Standing));
}

bool MantleRules::HasHeadroom(const FMantleConfig& Config, float FreeHeadroomCm, float CapsuleHeightCm)
{
	return FreeHeadroomCm < 0.f || FreeHeadroomCm >= CapsuleHeightCm + Config.HeadroomMarginCm;
}

FMantleDecision MantleRules::Decide(const FMantleConfig& Config, const FMantleProbe& Probe, TFunctionRef<bool(int32)> IsAvailable)
{
	FMantleDecision Result;
	if (Probe.HeightCm <= 0.f)
	{
		Result.Refusal = EMantleRefusal::NoObstacle;
		return Result;
	}
	if (!Probe.bEligible)
	{
		Result.Refusal = EMantleRefusal::NotEligible;
		return Result;
	}
	if (Probe.HeightCm > Config.CeilingCm)
	{
		Result.Refusal = EMantleRefusal::Wall;
		return Result;
	}
	const TOptional<EMantleDestination> Destination = DestinationForDepth(Config, Probe.DepthCm);
	if (!Destination.IsSet())
	{
		Result.Refusal = EMantleRefusal::DepthMismatch;
		return Result;
	}
	EMantleRefusal ClassRefusal = EMantleRefusal::None;
	Result.ClassIndex = ClassifyHeight(Config, Probe.HeightCm, *Destination, ClassRefusal);
	if (Result.ClassIndex == INDEX_NONE)
	{
		Result.Refusal = ClassRefusal;
		return Result;
	}
	const FMantleHeightClass& Class = Config.Classes[Result.ClassIndex];
	if (Class.bHandledByVault)
	{
		Result.Refusal = EMantleRefusal::VaultInstead;
		return Result;
	}
	if (!Class.bEnabled)
	{
		Result.Refusal = EMantleRefusal::ClassDisabled;
		return Result;
	}
	if (Class.bMarkedEdgeOnly && !Probe.bMarkedEdge)
	{
		Result.Refusal = EMantleRefusal::NotMarked;
		return Result;
	}
	if (!HasHeadroom(Config, Probe.FreeHeadroomCm, Probe.CapsuleHeightCm))
	{
		Result.Refusal = EMantleRefusal::NoHeadroom;
		return Result;
	}
	if (!Probe.bLandingFree)
	{
		Result.Refusal = EMantleRefusal::NoLanding;
		return Result;
	}
	if (Class.Destination == EMantleDestination::Over && FMath::Abs(Probe.LandingStepCm) > Config.OverMaxStepCm)
	{
		Result.Refusal = EMantleRefusal::LandingStep;
		return Result;
	}
	EMantleGait Gait = EMantleGait::Standing;
	bool bStopFirst = false;
	Result.ClipIndex = ChooseClip(Config, Class.Id, Probe.Body, Probe.bRunning, Probe.FaceDistanceCm, Probe.Rotation, IsAvailable, Gait, bStopFirst);
	if (Result.ClipIndex == INDEX_NONE)
	{
		Result.Refusal = EMantleRefusal::MissingClip;
		return Result;
	}
	const FMantleClip& Clip = Config.Clips[Result.ClipIndex];
	Result.Gait = Gait;
	Result.bStopFirst = bStopFirst;
	Result.ApproachCm = ClipStartCm(Config, Clip);
	// A standing climb farther from the face than its start + slack walks closer first (to the clip's start distance).
	Result.bApproachFirst = Gait == EMantleGait::Standing && Probe.FaceDistanceCm > Result.ApproachCm + Config.StandingSlackCm;
	const float Rise = FMath::Max(1.f, Clip.RiseCm);
	Result.VerticalWarp = Probe.HeightCm / Rise;
	Result.ForwardWarp = ForwardWarpFor(Config, Clip, Result.bApproachFirst ? Result.ApproachCm : Probe.FaceDistanceCm);
	if (FMath::Abs(Result.VerticalWarp - 1.f) > Config.MaxVerticalWarp)
	{
		Result.Refusal = EMantleRefusal::WarpTooLarge;
		return Result;
	}
	Result.Refusal = EMantleRefusal::None;
	return Result;
}

FMantleDecision MantleRules::Decide(const FMantleConfig& Config, const FMantleProbe& Probe)
{
	return Decide(Config, Probe, [](int32) { return true; });
}

FMantleClipContact MantleRules::AnalyseClipTrack(const FMantleConfig& Config, const FMantleClipTrack& Track, float RiseCm)
{
	FMantleClipContact Out;
	const int32 Num = Track.Num();
	if (Num < 3 || Track.SampleSeconds <= 0.f || Track.HandL.Num() != Num || Track.HandR.Num() != Num || Track.Pelvis.Num() != Num
		|| Track.FootL.Num() != Num || Track.FootR.Num() != Num)
	{
		return Out;
	}
	const float Dt = Track.SampleSeconds;
	Out.RootStart = Track.Root[0];
	Out.ExitSeconds = Track.TimeAt(Num - 1);
	const float Ground = static_cast<float>(FMath::Min(Track.FootL[0].Z, Track.FootR[0].Z));
	const float MinPlantZ = Ground + Config.AlignPlantMinRiseFraction * FMath::Max(0.f, RiseCm);
	const int32 Hold = FMath::Max(1, FMath::CeilToInt(Config.AlignPlantHoldSeconds / Dt - KINDA_SMALL_NUMBER));
	auto SpeedAt = [Dt](const TArray<FVector>& Bone, int32 Index)
	{
		const int32 A = FMath::Max(0, Index - 1);
		const int32 B = FMath::Min(Bone.Num() - 1, Index + 1);
		return B > A ? static_cast<float>(FVector::Dist(Bone[A], Bone[B])) / (Dt * static_cast<float>(B - A)) : 0.f;
	};
	// 1. The plant: per hand the first resting stretch after it reached out and up; the earlier hand wins.
	int32 Plant = INDEX_NONE;
	const TArray<FVector>* PlantHand = nullptr;
	for (const TArray<FVector>* Hand : { &Track.HandL, &Track.HandR })
	{
		bool bReached = false;
		for (int32 Index = 1; Index + Hold <= Num - 1 && (Plant == INDEX_NONE || Index < Plant); ++Index)
		{
			bReached |= FVector::Dist((*Hand)[Index], (*Hand)[0]) >= Config.AlignPlantReachCm;
			// A plant holds the body's climb: the root still on its way (a hand resting after the climb is the end pose, not a plant).
			if (!bReached || (*Hand)[Index].Z < MinPlantZ || FVector::Dist(Track.Root[Index], Track.Root.Last()) <= Config.AlignExitRootCm)
			{
				continue;
			}
			bool bRests = true;
			for (int32 Each = Index; Each < Index + Hold && bRests; ++Each)
			{
				bRests = SpeedAt(*Hand, Each) <= Config.AlignPlantSpeedCmPerSecond;
			}
			if (bRests)
			{
				Plant = Index;
				PlantHand = Hand;
				break;
			}
		}
	}
	if (Plant != INDEX_NONE)
	{
		Out.bValid = true;
		Out.ContactSeconds = Track.TimeAt(Plant);
		Out.HandPoint = (*PlantHand)[Plant];
		Out.RootAtContact = Track.Root[Plant];
	}
	// 2. The exit (batch 2, MantlingProblem_04 "hangs ~0.5 s in a strange pose"): after the plant, the first sample from which every bone
	// (root, pelvis, hands, feet) stays within align.exit_pose_cm of its final pose to the clip's end - what is left is a slow settle or an
	// idle tail, which the blend-out absorbs (the earlier rule waited for every bone to be still: the tall ledges' 0.5-0.6 s settle stood in it).
	const int32 From = FMath::Max(1, Plant);
	int32 Exit = Num - 1;
	for (int32 Index = Num - 1; Index >= From; --Index)
	{
		float Off = 0.f;
		for (const TArray<FVector>* Bone : { &Track.Root, &Track.Pelvis, &Track.HandL, &Track.HandR, &Track.FootL, &Track.FootR })
		{
			Off = FMath::Max(Off, static_cast<float>(FVector::Dist((*Bone)[Index], Bone->Last())));
		}
		if (Off > Config.AlignExitPoseCm)
		{
			break;
		}
		Exit = Index;
	}
	Out.ExitSeconds = Track.TimeAt(Exit);
	// The rule before batch 2 (Codex.Mantle.Batch2 0): the root at its last spot and every bone still.
	Out.StillExitSeconds = Track.TimeAt(Num - 1);
	for (int32 Index = From; Index < Num - 1; ++Index)
	{
		if (FVector::Dist(Track.Root[Index], Track.Root.Last()) > Config.AlignExitRootCm)
		{
			continue;
		}
		float Fastest = 0.f;
		for (const TArray<FVector>* Bone : { &Track.Pelvis, &Track.HandL, &Track.HandR, &Track.FootL, &Track.FootR })
		{
			Fastest = FMath::Max(Fastest, SpeedAt(*Bone, Index));
		}
		if (Fastest <= Config.AlignStillSpeedCmPerSecond)
		{
			Out.StillExitSeconds = Track.TimeAt(Index);
			break;
		}
	}
	return Out;
}

float MantleRules::ContactStartCm(const FMantleConfig& Config, const FMantleClipContact& Contact)
{
	if (!Contact.bValid)
	{
		return -1.f;
	}
	return FMath::Max(Config.MinFaceGapCm, static_cast<float>(Contact.HandPoint.X - Contact.RootStart.X) - Config.AlignGripForwardCm);
}

bool MantleRules::IsHeavyHit(const FMantleConfig& Config, float FinalDamage)
{
	return Config.HeavyHitDamage > 0.f && FinalDamage >= Config.HeavyHitDamage;
}

bool MantleRules::NeedsDownLink(const FMantleConfig& Config, float HeightCm)
{
	return HeightCm > 0.f && HeightCm < Config.LinkDownBelowCm;
}

TArray<float> MantleRules::LinkOffsets(const FMantleConfig& Config, float FaceLengthCm, float MarginCm)
{
	TArray<float> Offsets;
	if (FaceLengthCm < Config.LinkMinFaceCm)
	{
		return Offsets; // too short for a capsule to climb it squarely
	}
	const float Usable = FaceLengthCm - 2.f * FMath::Max(0.f, MarginCm);
	if (Usable <= 0.f)
	{
		Offsets.Add(0.f);
		return Offsets;
	}
	const int32 MaxCount = FMath::Max(1, FMath::RoundToInt(Config.LinkMaxPerFace));
	const int32 Count = FMath::Clamp(FMath::FloorToInt(Usable / FMath::Max(1.f, Config.LinkSpacingCm)) + 1, 1, MaxCount);
	if (Count == 1)
	{
		Offsets.Add(0.f);
		return Offsets;
	}
	const float Step = Usable / static_cast<float>(Count - 1);
	for (int32 Index = 0; Index < Count; ++Index)
	{
		Offsets.Add(-Usable * 0.5f + Step * Index);
	}
	return Offsets;
}

int32 MantleRules::ChooseDescent(const TArray<FMantleDescentOption>& Options)
{
	int32 Best = INDEX_NONE;
	for (int32 Index = 0; Index < Options.Num(); ++Index)
	{
		const FMantleDescentOption& Option = Options[Index];
		if (!Option.bValid)
		{
			continue;
		}
		if (Best == INDEX_NONE)
		{
			Best = Index;
			continue;
		}
		const FMantleDescentOption& Held = Options[Best];
		if (Option.bPathComplete != Held.bPathComplete)
		{
			Best = Option.bPathComplete ? Index : Best;
			continue;
		}
		if (Option.WalkCm + Option.PathCm < Held.WalkCm + Held.PathCm)
		{
			Best = Index;
		}
	}
	return Best;
}

FVector MantleRules::DescentExitOnFace(const FVector& A, const FVector& B, const FVector& Goal, float MarginCm)
{
	const FVector2D From(A);
	const FVector2D Along = FVector2D(B) - From;
	const float Length = static_cast<float>(Along.Size());
	if (Length <= KINDA_SMALL_NUMBER)
	{
		return A;
	}
	const FVector2D Unit = Along / Length;
	const float Margin = FMath::Max(0.f, MarginCm);
	const float At = Length <= 2.f * Margin ? Length * 0.5f
		: FMath::Clamp(static_cast<float>(FVector2D::DotProduct(FVector2D(Goal) - From, Unit)), Margin, Length - Margin);
	const FVector2D Point = From + Unit * At;
	return FVector(Point.X, Point.Y, A.Z);
}
