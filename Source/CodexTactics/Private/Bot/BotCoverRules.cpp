// The playtest bot's cover at a wall in a fight (game designer decision 5, 2026-11-01 night, docs/design/COMBAT_LAB.md section 7.2).
// UE-only, no Godot reference. Tests: CodexTactics.Bot.Cover.*.

#include "Bot/BotCoverRules.h"

float BotCoverRules::Score(float DistanceCm, float FacingCos, const FBotCoverParams& Params)
{
	return FacingCos - 0.8f * DistanceCm / FMath::Max(1.f, Params.SearchCm);
}

TOptional<FBotCoverChoice> BotCoverRules::EvaluateFace(const FPressureBox& Box, int32 BoxIndex, int32 FaceSign, const FVector2D& Leader,
	const FVector2D& Threat, const FBotCoverParams& Params)
{
	FVector2D Axis;
	float HalfLength = 0.f;
	float HalfThickness = 0.f;
	AmbushSpotRules::LongAxis(Box, Axis, HalfLength, HalfThickness);
	if (2.f * HalfLength < Params.MinWallLengthCm || (Box.HeightCm > 0.f && Box.HeightCm < Params.MinHeightCm))
	{
		return {};
	}
	const FVector2D Normal = FVector2D(-Axis.Y, Axis.X) * static_cast<double>(FaceSign >= 0 ? 1 : -1);
	const float Reach = FMath::Max(0.f, HalfLength - Params.EndInsetCm);
	const float Along = FMath::Clamp(static_cast<float>(FVector2D::DotProduct(Leader - Box.Center, Axis)), -Reach, Reach);
	FBotCoverChoice Choice;
	Choice.Box = BoxIndex;
	Choice.Normal = Normal;
	Choice.FacePoint = Box.Center + Axis * Along + Normal * HalfThickness;
	const FVector2D ToThreat = Threat - Choice.FacePoint;
	const float ThreatCm = static_cast<float>(ToThreat.Size());
	if (ThreatCm < Params.MinThreatCm)
	{
		return {};
	}
	// The wall stands between: the threat lies behind the face (against its normal), roughly square to it.
	Choice.FacingCos = static_cast<float>(FVector2D::DotProduct(-Normal, ToThreat / ThreatCm));
	if (Choice.FacingCos < Params.MinFacingCos)
	{
		return {};
	}
	// The walk there: straight when the squad is on this face's side; round the nearer end of the wall when it is on the far side.
	Choice.DistanceCm = static_cast<float>(FVector2D::Distance(Leader, Choice.FacePoint));
	if (FVector2D::DotProduct(Leader - Choice.FacePoint, Normal) < 0.0)
	{
		float Round = MAX_flt;
		for (const float Sign : { 1.f, -1.f })
		{
			const FVector2D End = Box.Center + Axis * static_cast<double>(Sign * (HalfLength + 50.f)) + Normal * HalfThickness;
			Round = FMath::Min(Round, static_cast<float>(FVector2D::Distance(Leader, End) + FVector2D::Distance(End, Choice.FacePoint)));
		}
		Choice.DistanceCm = Round;
	}
	if (Choice.DistanceCm > Params.SearchCm)
	{
		return {};
	}
	Choice.Score = Score(Choice.DistanceCm, Choice.FacingCos, Params);
	return Choice;
}

TOptional<FBotCoverChoice> BotCoverRules::PickWall(const TArray<FPressureBox>& Boxes, const FVector2D& Leader, const FVector2D& Threat,
	const FBotCoverParams& Params)
{
	TOptional<FBotCoverChoice> Best;
	for (int32 Index = 0; Index < Boxes.Num(); ++Index)
	{
		for (const int32 Face : { 1, -1 })
		{
			const TOptional<FBotCoverChoice> Choice = EvaluateFace(Boxes[Index], Index, Face, Leader, Threat, Params);
			if (Choice.IsSet() && (!Best.IsSet() || Choice->Score > Best->Score))
			{
				Best = Choice;
			}
		}
	}
	return Best;
}
