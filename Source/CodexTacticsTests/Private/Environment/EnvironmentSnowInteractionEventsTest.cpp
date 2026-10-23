#include "Misc/AutomationTest.h"
#include "Core/PlayerCommands.h"
#include "Environment/SnowInteractionRules.h"
#include "Environment/SnowStampSet.h"

#if WITH_DEV_AUTOMATION_TESTS

// Interactive snow, phase 3 (UE-only; docs/systems/environment-weather.md section 14, docs/design/INTERACTIVE_SNOW.md section 5): the pure
// rules of the event stamps - the explosion crater (shape params from the GameplayEvents::Explosion kind / radius, the stamps it makes),
// the bullet-dent ray / queue / per-frame trace cap and the dent itself, and the drag trail of a real-time push polled at 20 Hz.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnowInteractionCraterTest, "CodexTactics.Environment.SnowInteraction.CraterShape",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnowInteractionCraterTest::RunTest(const FString&)
{
	using namespace SnowInteractionRules;
	namespace Kinds = GameplayEvents::ExplosionKinds;
	const FSnowStampTable Table;
	const FSnowInteractionTuning Tuning;

	// Kind -> spec.
	TestTrue(TEXT("grenade spec"), &GetCraterSpec(Kinds::Grenade, Table) == &Table.GrenadeCrater);
	TestTrue(TEXT("tripwire = two grenades"), &GetCraterSpec(Kinds::Tripwire, Table) == &Table.GrenadeCrater);
	TestTrue(TEXT("barrel spec"), &GetCraterSpec(Kinds::Barrel, Table) == &Table.BarrelCrater);
	TestTrue(TEXT("mine spec"), &GetCraterSpec(Kinds::Mine, Table) == &Table.MineCrater);
	TestTrue(TEXT("cannon spec"), &GetCraterSpec(Kinds::Cannon, Table) == &Table.CannonCrater);
	TestTrue(TEXT("trap = generic crater"), &GetCraterSpec(Kinds::Trap, Table) == &Table.Crater);
	TestTrue(TEXT("unknown kind = generic crater"), &GetCraterSpec(FName(TEXT("Whatever")), Table) == &Table.Crater);
	for (const FSnowStampSpec* Spec : { &Table.Crater, &Table.GrenadeCrater, &Table.BarrelCrater, &Table.MineCrater, &Table.CannonCrater })
	{
		TestEqual(TEXT("crater shape"), Spec->Shape, ESnowStampShape::Crater);
		TestFalse(TEXT("craters are world-aligned"), Spec->bAlignToMotion);
		TestTrue(TEXT("a bowl and a rim within the RT range"), Spec->Depth > 0.f && Spec->Depth <= MaxChannelValue && Spec->Berm > 0.f
			&& Spec->Berm <= MaxChannelValue);
	}
	// A buried mine digs deepest, a fuel barrel (the blast goes up) the least.
	TestTrue(TEXT("mine deeper than a grenade"), Table.MineCrater.Depth > Table.GrenadeCrater.Depth);
	TestTrue(TEXT("barrel shallower than a grenade"), Table.BarrelCrater.Depth < Table.GrenadeCrater.Depth);
	TestEqual(TEXT("spray ring: no bowl"), Table.CraterSpray.Depth, 0.f);
	TestTrue(TEXT("spray ring: thrown snow"), Table.CraterSpray.Berm > 0.f);
	TestTrue(TEXT("clod: a small pit with a rim"), Table.CraterClod.Depth > 0.f && Table.CraterClod.Berm > Table.CraterClod.Depth);

	// Diameter from the blast radius (x 0.55, clamped 120..380 cm); no radius = the kind's own size.
	const FSnowCraterParams Grenade = ComputeCrater(Kinds::Grenade, 400.f, Table, Tuning);
	TestEqual(TEXT("grenade 4 m blast -> 2.2 m crater"), Grenade.DiameterCm, 400.f * Tuning.CraterDiameterPerRadius, 0.01f);
	TestEqual(TEXT("grenade depth from its spec"), Grenade.Depth, Table.GrenadeCrater.Depth);
	TestEqual(TEXT("grenade rim from its spec"), Grenade.Berm, Table.GrenadeCrater.Berm);
	TestEqual(TEXT("barrel 5.5 m blast"), ComputeCrater(Kinds::Barrel, 550.f, Table, Tuning).DiameterCm, 302.5f, 0.01f);
	TestEqual(TEXT("cannon 1.5 m splash: clamped up to the minimum"), ComputeCrater(Kinds::Cannon, 150.f, Table, Tuning).DiameterCm,
		Tuning.CraterMinCm, 0.01f);
	TestEqual(TEXT("a huge blast: clamped to the maximum"), ComputeCrater(Kinds::Trap, 5000.f, Table, Tuning).DiameterCm, Tuning.CraterMaxCm, 0.01f);
	TestEqual(TEXT("no radius: the kind's own size"), ComputeCrater(Kinds::Mine, 0.f, Table, Tuning).DiameterCm, Table.MineCrater.LengthCm, 0.01f);
	TestEqual(TEXT("turn-based barrel (no radius)"), ComputeCrater(Kinds::Barrel, 0.f, Table, Tuning).DiameterCm, Table.BarrelCrater.LengthCm, 0.01f);
	TestTrue(TEXT("a bigger blast = a bigger crater"), ComputeCrater(Kinds::Grenade, 500.f, Table, Tuning).DiameterCm > Grenade.DiameterCm);
	TestEqual(TEXT("spray ring = 1.7 x the crater"), Grenade.SprayDiameterCm, Grenade.DiameterCm * Tuning.CraterSprayScale, 0.01f);
	TestEqual(TEXT("spray berm from the spec"), Grenade.SprayBerm, Table.CraterSpray.Berm);
	TestEqual(TEXT("clods"), Grenade.Clods, Tuning.CraterClods);

	// The stamps: the bowl (R in the middle, G on its rim), the spray ring (G only), the clods round it.
	const FVector2D Centre(1234., -567.);
	TArray<FSnowStamp> Stamps;
	const int32 Added = MakeCraterStamps(Grenade, Table, Centre, Stamps);
	TestEqual(TEXT("bowl + pit + ring + clods"), Added, 3 + Tuning.CraterClods);
	TestEqual(TEXT("appended"), Stamps.Num(), Added);
	if (Stamps.Num() == Added && Added >= 3)
	{
		const FSnowStamp& Bowl = Stamps[0];
		TestEqual(TEXT("bowl shape"), Bowl.Shape, ESnowStampShape::Crater);
		TestTrue(TEXT("bowl centred"), Bowl.Centre.Equals(Centre, 0.01));
		TestEqual(TEXT("bowl length = diameter"), Bowl.LengthCm, Grenade.DiameterCm, 0.01f);
		TestTrue(TEXT("bowl slightly oval (0.92..1.08)"), Bowl.WidthCm >= Grenade.DiameterCm * 0.92f - 0.01f && Bowl.WidthCm <= Grenade.DiameterCm * 1.08f + 0.01f);
		TestEqual(TEXT("bowl depth"), Bowl.Depth, Grenade.Depth);
		TestEqual(TEXT("bowl rim"), Bowl.Berm, Grenade.Berm);
		const FSnowStamp& Pit = Stamps[1];
		TestEqual(TEXT("pit shape (steep walls)"), Pit.Shape, ESnowStampShape::Body);
		TestTrue(TEXT("pit centred"), Pit.Centre.Equals(Centre, 0.01));
		TestEqual(TEXT("pit size"), Pit.LengthCm, Grenade.DiameterCm * CraterPitScale, 0.01f);
		TestTrue(TEXT("pit: deepens, no rim"), Pit.Depth > 0.f && Pit.Berm == 0.f);
		const FSnowStamp& Spray = Stamps[2];
		TestEqual(TEXT("ring shape"), Spray.Shape, ESnowStampShape::Crater);
		TestEqual(TEXT("ring: no compaction"), Spray.Depth, 0.f);
		TestTrue(TEXT("ring: berm"), Spray.Berm > 0.f);
		TestTrue(TEXT("ring outside the bowl"), Spray.LengthCm > Bowl.LengthCm && Spray.WidthCm > Bowl.WidthCm);
		for (int32 Index = 3; Index < Stamps.Num(); ++Index)
		{
			const FSnowStamp& Clod = Stamps[Index];
			const double Distance = FVector2D::Distance(Clod.Centre, Centre);
			TestEqual(TEXT("clod shape"), Clod.Shape, ESnowStampShape::Dent);
			TestEqual(TEXT("clod depth from its spec"), Clod.Depth, Table.CraterClod.Depth);
			TestTrue(FString::Printf(TEXT("clod %d at 0.6..1.15 x the diameter (%.0f cm)"), Index, Distance),
				Distance >= Grenade.DiameterCm * 0.6 - 0.5 && Distance <= Grenade.DiameterCm * 1.15 + 0.5);
		}
	}
	// Deterministic (the game's random stream is never touched): the same blast spot gives the same crater, another spot another layout.
	TArray<FSnowStamp> Again;
	MakeCraterStamps(Grenade, Table, Centre, Again);
	bool bSame = Again.Num() == Stamps.Num();
	for (int32 Index = 0; bSame && Index < Again.Num(); ++Index)
	{
		bSame = Again[Index].Centre.Equals(Stamps[Index].Centre, 1e-6) && Again[Index].YawDeg == Stamps[Index].YawDeg
			&& Again[Index].LengthCm == Stamps[Index].LengthCm;
	}
	TestTrue(TEXT("same spot -> same crater"), bSame);
	TArray<FSnowStamp> Elsewhere;
	MakeCraterStamps(Grenade, Table, Centre + FVector2D(731., 219.), Elsewhere);
	TestTrue(TEXT("another spot -> other clods"), Elsewhere.Num() > 3 && !(Elsewhere[3].Centre - FVector2D(731., 219.)).Equals(Stamps[3].Centre, 1.));
	FSnowCraterParams NoClods = Grenade;
	NoClods.Clods = 0;
	NoClods.SprayBerm = 0.f;
	TArray<FSnowStamp> Bare;
	TestEqual(TEXT("no spray, no clods: the bowl + its pit"), MakeCraterStamps(NoClods, Table, Centre, Bare), 2);
	TestEqual(TEXT("an empty crater makes nothing"), MakeCraterStamps(FSnowCraterParams(), Table, Centre, Bare), 0);
	// The shape ids survive the vertex colour (M_SnowStamp: 3 crater, 4 dent).
	TestEqual(TEXT("crater id in the vertex colour"), DecodeStampShape(EncodeStampColor(ESnowStampShape::Crater, 1.5f, 1.1f).ToFColor(true)), 3);
	TestEqual(TEXT("dent id in the vertex colour"), DecodeStampShape(EncodeStampColor(ESnowStampShape::Dent, 0.f, 0.7f).ToFColor(true)), 4);
	// The data asset class has the same defaults (DA_SnowStamps loads the new fields from them).
	const USnowStampSet* Defaults = GetDefault<USnowStampSet>();
	TestEqual(TEXT("asset class: grenade crater"), Defaults->Stamps.GrenadeCrater.LengthCm, Table.GrenadeCrater.LengthCm);
	TestEqual(TEXT("asset class: crater per radius"), Defaults->Tuning.CraterDiameterPerRadius, Tuning.CraterDiameterPerRadius);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnowInteractionDentTest, "CodexTactics.Environment.SnowInteraction.DentTraceCap",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnowInteractionDentTest::RunTest(const FString&)
{
	using namespace SnowInteractionRules;
	const FSnowStampTable Table;
	const FSnowInteractionTuning Tuning;
	TestEqual(TEXT("rays extended 15 m"), Tuning.ShotExtendCm, 1500.f);
	TestEqual(TEXT("at most 8 dent traces a frame"), Tuning.ShotTracesPerFrame, 8);

	// The ray: from 30 cm before the tracer's end to 15 m past it, along the shot.
	FVector From;
	FVector To;
	TestTrue(TEXT("a shot has a ray"), ComputeShotRay(FVector(0., 0., 150.), FVector(1000., 0., 100.), 1500.f, 30.f, From, To));
	const FVector Dir = (FVector(1000., 0., 100.) - FVector(0., 0., 150.)).GetSafeNormal();
	TestTrue(TEXT("starts 30 cm before the end"), From.Equals(FVector(1000., 0., 100.) - Dir * 30., 0.01));
	TestTrue(TEXT("ends 15 m past the end"), To.Equals(FVector(1000., 0., 100.) + Dir * 1500., 0.01));
	TestTrue(TEXT("a short tracer: the start is the muzzle"), ComputeShotRay(FVector::ZeroVector, FVector(10., 0., 0.), 1500.f, 30.f, From, To)
		&& From.Equals(FVector::ZeroVector, 0.01));
	TestFalse(TEXT("no length: no ray"), ComputeShotRay(FVector(5., 5., 5.), FVector(5., 5., 5.), 1500.f, 30.f, From, To));

	// The queue drops its oldest when full; the per-frame budget is capped.
	TArray<FSnowShot> Queue;
	int32 Dropped = 0;
	for (int32 Index = 0; Index < 40; ++Index)
	{
		FSnowShot Shot;
		Shot.End = FVector(Index, 0., 0.);
		Dropped += EnqueueShot(Queue, Shot, Tuning.ShotQueueMax);
	}
	TestEqual(TEXT("queue capped"), Queue.Num(), Tuning.ShotQueueMax);
	TestEqual(TEXT("the oldest dropped"), Dropped, 40 - Tuning.ShotQueueMax);
	TestEqual(TEXT("the oldest kept is #8"), Queue[0].End.X, 40. - Tuning.ShotQueueMax);
	TestEqual(TEXT("the newest is last"), Queue.Last().End.X, 39.);
	// An MG burst of 40 tracers in one frame: never more than 8 traces a frame, the queue drains in 4 frames.
	int32 Frames = 0;
	int32 MaxInFrame = 0;
	while (!Queue.IsEmpty() && Frames < 100)
	{
		const int32 Budget = GetShotBudget(Queue.Num(), Tuning.ShotTracesPerFrame);
		MaxInFrame = FMath::Max(MaxInFrame, Budget);
		Queue.RemoveAt(0, Budget);
		++Frames;
	}
	TestEqual(TEXT("at most 8 a frame"), MaxInFrame, 8);
	TestEqual(TEXT("32 queued -> 4 frames"), Frames, 4);
	TestEqual(TEXT("budget of an empty queue"), GetShotBudget(0, 8), 0);
	TestEqual(TEXT("budget of a short queue"), GetShotBudget(3, 8), 3);
	TestEqual(TEXT("budget never negative"), GetShotBudget(5, -2), 0);
	TestEqual(TEXT("queue of 1 keeps the newest"), [&]() { TArray<FSnowShot> Q; FSnowShot A; A.End.X = 1.; FSnowShot B; B.End.X = 2.; EnqueueShot(Q, A, 1);
		EnqueueShot(Q, B, 1); return Q.Num() == 1 ? Q[0].End.X : -1.; }(), 2.);

	// The dent: a steep shot = the spec's round dent; a grazing one is stretched along the ray (at most 2x).
	const FSnowStamp Steep = MakeDent(Table.Dent, FVector(100., 200., 0.), FVector(0.3, 0., -1.), FVector::UpVector, Tuning.DentMaxStretch);
	TestEqual(TEXT("dent shape"), Steep.Shape, ESnowStampShape::Dent);
	TestTrue(TEXT("dent at the hit"), Steep.Centre.Equals(FVector2D(100., 200.), 0.01));
	TestTrue(TEXT("steep: about the spec size"), Steep.LengthCm <= Table.Dent.LengthCm * (1.f + Table.Dent.RandomScale) * 1.05f + 0.01f);
	TestEqual(TEXT("dent depth"), Steep.Depth, Table.Dent.Depth);
	const FSnowStamp Grazing = MakeDent(Table.Dent, FVector(100., 200., 0.), FVector(0., 1., -0.1), FVector::UpVector, Tuning.DentMaxStretch);
	TestEqual(TEXT("grazing: stretched to the max"), Grazing.LengthCm / Grazing.WidthCm, Tuning.DentMaxStretch, 0.01f);
	TestEqual(TEXT("grazing: along the ray (+Y)"), Grazing.YawDeg, 90.f, 0.01f);
	const FSnowStamp Thirty = MakeDent(Table.Dent, FVector(100., 200., 0.), FVector(FMath::Cos(PI / 6.), 0., -FMath::Sin(PI / 6.)), FVector::UpVector, 4.f);
	TestEqual(TEXT("30 degrees: 1 / sin 30 = 2x"), Thirty.LengthCm / Thirty.WidthCm, 2.f, 0.01f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnowInteractionDragTest, "CodexTactics.Environment.SnowInteraction.DragSegments",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnowInteractionDragTest::RunTest(const FString&)
{
	using namespace SnowInteractionRules;
	const FSnowStampTable Table;
	const FSnowInteractionTuning Tuning;
	const FSnowStampSpec& Drag = Table.DragTrail;

	// A real-time push: the object rides in front of the worker at ~1.5 m/s, polled at 20 Hz (URelocationSubsystem::GetMovingObjects).
	const float Speed = 150.f;
	const float Dt = 1.f / Tuning.PollHz;
	FSnowTrailState Trail;
	TArray<FSnowStamp> Segments;
	FVector2D Position(500., 500.);
	const FVector2D Heading = FVector2D(3., 4.).GetSafeNormal();
	const int32 Polls = FMath::RoundToInt(5.f / Dt); // 5 s = 7.5 m
	for (int32 Poll = 0; Poll <= Polls; ++Poll)
	{
		AdvanceTrail(Trail, Position, Drag, Tuning.MaxDragCm, Segments);
		Position += Heading * Speed * Dt;
	}
	const double Covered = Speed * Dt * Polls;
	double Total = 0.;
	bool bContiguous = true;
	bool bWidth = true;
	for (int32 Index = 0; Index < Segments.Num(); ++Index)
	{
		const FSnowStamp& S = Segments[Index];
		Total += S.LengthCm;
		bWidth &= S.WidthCm == Drag.WidthCm && S.Shape == ESnowStampShape::Segment && S.Depth == Drag.Depth;
		if (Index > 0)
		{
			// Each segment starts where the previous one ended (no gaps, no overlaps).
			const FSnowStamp& P = Segments[Index - 1];
			const FVector2D PrevEnd = P.Centre + FVector2D(FMath::Cos(FMath::DegreesToRadians(P.YawDeg)), FMath::Sin(FMath::DegreesToRadians(P.YawDeg))) * (P.LengthCm * 0.5);
			const FVector2D Start = S.Centre - FVector2D(FMath::Cos(FMath::DegreesToRadians(S.YawDeg)), FMath::Sin(FMath::DegreesToRadians(S.YawDeg))) * (S.LengthCm * 0.5);
			bContiguous &= PrevEnd.Equals(Start, 0.05);
		}
	}
	TestTrue(FString::Printf(TEXT("segments every ~%.0f cm (%d for %.0f cm)"), Drag.SpacingCm, Segments.Num(), Covered),
		Segments.Num() >= FMath::FloorToInt32(Covered / (Drag.SpacingCm + Speed * Dt)) && Segments.Num() <= FMath::CeilToInt32(Covered / Drag.SpacingCm));
	TestTrue(FString::Printf(TEXT("the trail covers the push (%.0f of %.0f cm, short by < one spacing)"), Total, Covered),
		Total <= Covered + 0.5 && Covered - Total < Drag.SpacingCm);
	TestTrue(TEXT("contiguous"), bContiguous);
	TestTrue(TEXT("drag trail width / shape / depth"), bWidth);
	TestTrue(TEXT("each segment at least the spacing"), Segments.ContainsByPredicate([&](const FSnowStamp& S) { return S.LengthCm < Drag.SpacingCm - 0.01f; }) == false);
	TestEqual(TEXT("along the push"), Segments.Num() > 0 ? Segments[0].YawDeg : 0.f, static_cast<float>(FMath::RadiansToDegrees(FMath::Atan2(4., 3.))), 0.01f);

	// Standing still: nothing more.
	const int32 Before = Segments.Num();
	for (int32 Poll = 0; Poll < 40; ++Poll)
	{
		AdvanceTrail(Trail, Position, Drag, Tuning.MaxDragCm, Segments);
	}
	TestEqual(TEXT("a parked object leaves nothing"), Segments.Num(), Before);
	// A turn-based relocation moves the object at once: a jump of up to MaxDragCm (15 m) is still one segment; a longer one restarts.
	TestEqual(TEXT("10 m jump = one segment"), AdvanceTrail(Trail, Trail.Last + FVector2D(1000., 0.), Drag, Tuning.MaxDragCm, Segments), 1);
	TestEqual(TEXT("20 m jump restarts the trail"), AdvanceTrail(Trail, Trail.Last + FVector2D(2000., 0.), Drag, Tuning.MaxDragCm, Segments), 0);
	TestEqual(TEXT("tracked 10 s after the push"), Tuning.DragTrackSeconds, 10.f);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
