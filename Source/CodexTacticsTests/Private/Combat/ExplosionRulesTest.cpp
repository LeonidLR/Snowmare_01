// CodexTactics.Weapons.Explosion.* - the shared explosion model (2026-11-09, docs/design/WEAPONS_FRAMEWORK.md 16): the falloff family, the cover
// rule (a wall cuts the blast, a plunging round only stops at a roof), the armour class of a blast, the RPG-7 back-blast cone, the camera trauma,
// the "explosions" data, and the parity of the grenade / the placed charges moved onto it (the same numbers as their old inline code).

#include "Combat/ArmorRules.h"
#include "Combat/Explosion.h"
#include "Combat/ExplosionRules.h"
#include "Combat/GrenadeRules.h"
#include "Core/PlayerCommands.h"
#include "Data/WeaponStats.h"
#include "Dom/JsonObject.h"
#include "Interactables/DeployableRules.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace ExplosionRulesTest
{
	TSharedPtr<FJsonObject> ParseExplosionJson(const TCHAR* Text)
	{
		TSharedPtr<FJsonObject> Root;
		FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root);
		return Root;
	}

	TSharedPtr<FJsonObject> ProjectExplosionsBlock()
	{
		FString Text;
		TSharedPtr<FJsonObject> Root;
		const TSharedPtr<FJsonObject>* Block = nullptr;
		if (FFileHelper::LoadFileToString(Text, *(FPaths::ProjectContentDir() / TEXT("Data/Weapons/weapons_tuning.json")))
			&& FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) && Root.IsValid()
			&& Root->TryGetObjectField(TEXT("explosions"), Block) && Block)
		{
			return *Block;
		}
		return nullptr;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FExplosionFalloffTest, "CodexTactics.Weapons.Explosion.Falloff",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FExplosionFalloffTest::RunTest(const FString&)
{
	FBlastFalloff Half;
	Half.Model = EBlastFalloff::HalfAtEdge;
	TestEqual(TEXT("half-at-edge: full at the centre"), ExplosionRules::Falloff(Half, 0.f, 400.f), 1.f);
	TestEqual(TEXT("half-at-edge: 75 % half way"), ExplosionRules::Falloff(Half, 200.f, 400.f), 0.75f);
	TestEqual(TEXT("half-at-edge: half at the edge (the edge counts)"), ExplosionRules::Falloff(Half, 400.f, 400.f), 0.5f);
	TestEqual(TEXT("half-at-edge: nothing past the edge"), ExplosionRules::Falloff(Half, 401.f, 400.f), 0.f);

	FBlastFalloff Linear;
	Linear.Model = EBlastFalloff::Linear;
	Linear.Factor = 0.4f;
	TestEqual(TEXT("linear 0.4: 60 % at the edge"), ExplosionRules::Falloff(Linear, 550.f, 550.f), 0.6f, 1e-6f);
	TestEqual(TEXT("linear 0.4: 80 % half way"), ExplosionRules::Falloff(Linear, 275.f, 550.f), 0.8f, 1e-6f);
	TestEqual(TEXT("linear: nothing with no radius"), ExplosionRules::Falloff(Linear, 0.f, 0.f), 0.f);

	FWeaponExplosionStats Warhead;
	Warhead.RadiusCm = 500.f;
	Warhead.FullDamageRadiusCm = 250.f;
	Warhead.EdgeDamageMultiplier = 0.1f;
	const FBlastFalloff Core = ExplosionRules::FromWeapon(Warhead);
	TestTrue(TEXT("a weapon block = the full-core curve"), Core.Model == EBlastFalloff::FullCore);
	TestEqual(TEXT("full core: full inside the core"), ExplosionRules::Falloff(Core, 250.f, 500.f), 1.f);
	TestEqual(TEXT("full core: the edge multiplier at the radius"), ExplosionRules::Falloff(Core, 500.f, 500.f), 0.1f, 1e-6f);
	TestEqual(TEXT("full core: half way down the ramp"), ExplosionRules::Falloff(Core, 375.f, 500.f), 0.55f, 1e-6f);

	TestEqual(TEXT("victim damage = base x falloff x scale x cover"), ExplosionRules::VictimDamage(100.f, Half, 200.f, 400.f, 0.65f, 0.25f),
		100.f * 0.75f * 0.65f * 0.25f, 1e-4f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FExplosionOcclusionTest, "CodexTactics.Weapons.Explosion.Occlusion",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FExplosionOcclusionTest::RunTest(const FString&)
{
	FBlastOcclusionConfig Config;
	TestEqual(TEXT("open ground: full blast"), ExplosionRules::CoverMultiplier(false, false, false, Config), 1.f);
	TestEqual(TEXT("a wall between cuts the blast (x0.25)"), ExplosionRules::CoverMultiplier(false, true, false, Config), 0.25f);
	TestEqual(TEXT("a roof does not matter to a normal blast"), ExplosionRules::CoverMultiplier(false, false, true, Config), 1.f);
	TestEqual(TEXT("plunging: a wall does not protect"), ExplosionRules::CoverMultiplier(true, true, false, Config), 1.f);
	TestEqual(TEXT("plunging: a roof protects (x0)"), ExplosionRules::CoverMultiplier(true, true, true, Config), 0.f);
	Config.WallMultiplier = 0.f;
	Config.RoofMultiplier = 0.5f;
	TestEqual(TEXT("a wall can stop it whole (data)"), ExplosionRules::CoverMultiplier(false, true, false, Config), 0.f);
	TestEqual(TEXT("a light roof only halves it (data)"), ExplosionRules::CoverMultiplier(true, false, true, Config), 0.5f);

	const FExplosionConfig Defaults = ExplosionRules::Defaults();
	TestTrue(TEXT("default: the cover rule on for projectiles"), Defaults.UsesOcclusion(ExplosionRules::ProjectileKind()));
	TestFalse(TEXT("default: off for the grenade (parity)"), Defaults.UsesOcclusion(GameplayEvents::ExplosionKinds::Grenade));
	TestFalse(TEXT("default: off for the barrel (parity)"), Defaults.UsesOcclusion(GameplayEvents::ExplosionKinds::Barrel));
	TestEqual(TEXT("default: no camera shake (as before)"), Defaults.CameraTrauma, 0.f);

	TArray<FString> Errors;
	const TSharedPtr<FJsonObject> Good = ExplosionRulesTest::ParseExplosionJson(
		TEXT("{\"wall_multiplier\": 0.1, \"roof_multiplier\": 0.2, \"camera_trauma\": 0.3, \"occlusion\": {\"grenade\": true, \"projectile\": false}}"));
	const FExplosionConfig Read = ExplosionRules::Parse(*Good, &Errors);
	TestEqual(TEXT("parse: no errors"), Errors.Num(), 0);
	TestEqual(TEXT("parse: wall"), Read.Occlusion.WallMultiplier, 0.1f);
	TestEqual(TEXT("parse: roof"), Read.Occlusion.RoofMultiplier, 0.2f);
	TestEqual(TEXT("parse: trauma"), Read.CameraTrauma, 0.3f);
	TestTrue(TEXT("parse: grenade on (kind names ignore case)"), Read.UsesOcclusion(GameplayEvents::ExplosionKinds::Grenade));
	TestFalse(TEXT("parse: projectile off"), Read.UsesOcclusion(ExplosionRules::ProjectileKind()));

	Errors.Reset();
	const FExplosionConfig Bad = ExplosionRules::Parse(*ExplosionRulesTest::ParseExplosionJson(TEXT("{\"wall_multiplier\": 2, \"occlusion\": {\"mine\": 1}}")), &Errors);
	TestEqual(TEXT("bad data: two errors"), Errors.Num(), 2);
	TestEqual(TEXT("bad data: the default wall kept"), Bad.Occlusion.WallMultiplier, 0.25f);

	const TSharedPtr<FJsonObject> Project = ExplosionRulesTest::ProjectExplosionsBlock();
	if (TestTrue(TEXT("weapons_tuning.json has an explosions block"), Project.IsValid()))
	{
		Errors.Reset();
		const FExplosionConfig Shipped = ExplosionRules::Parse(*Project, &Errors);
		TestEqual(TEXT("the shipped block reads cleanly"), Errors.Num(), 0);
		for (const FName Kind : { GameplayEvents::ExplosionKinds::Grenade, GameplayEvents::ExplosionKinds::Barrel, GameplayEvents::ExplosionKinds::Mine,
			GameplayEvents::ExplosionKinds::Tripwire, GameplayEvents::ExplosionKinds::Trap })
		{
			TestFalse(FString::Printf(TEXT("shipped: no cover rule on %s (parity)"), *Kind.ToString()), Shipped.UsesOcclusion(Kind));
		}
		TestTrue(TEXT("shipped: the cover rule on projectiles"), Shipped.UsesOcclusion(ExplosionRules::ProjectileKind()));
		TestEqual(TEXT("shipped: no camera shake yet"), Shipped.CameraTrauma, 0.f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FExplosionArmorClassTest, "CodexTactics.Weapons.Explosion.ArmorClass",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FExplosionArmorClassTest::RunTest(const FString&)
{
	// Decision 1: explosive damage is the vehicle class - its penetration applies to every body, the hulls included.
	const FExplosionSpec Grenade = Explosions::GrenadeSpec(FVector::ZeroVector, 85.f, 400.f, TEXT("Grenade"), nullptr);
	const FExplosionSpec Charge = Explosions::ChargeSpec(FVector::ZeroVector, 120.f, 80.f, 550.f, 0.4f, EDamageType::Fire, TEXT("Barrel"), nullptr);
	TestTrue(TEXT("the grenade's blast is class Vehicle"), Grenade.PenetrationClass == EArmorClass::Vehicle);
	TestTrue(TEXT("a placed charge's blast is class Vehicle"), Charge.PenetrationClass == EArmorClass::Vehicle);
	TestTrue(TEXT("default damage spec = class Vehicle (the old blasts used it)"), FDamageSpec().PenetrationClass == EArmorClass::Vehicle);
	for (const EArmorClass Body : { EArmorClass::None, EArmorClass::Light, EArmorClass::Medium, EArmorClass::Heavy, EArmorClass::Vehicle })
	{
		TestEqual(FString::Printf(TEXT("a blast's penetration applies to class %d"), static_cast<int32>(Body)),
			ArmorRules::EffectiveReduction(0.5f, false, Charge.ArmorPenetration, Charge.PenetrationClass, Body), 0.5f * (1.f - 0.4f), 1e-6f);
	}
	TestEqual(TEXT("a light round's penetration does not apply to a heavy body (the contrast)"),
		ArmorRules::EffectiveReduction(0.5f, false, 0.4f, EArmorClass::Light, EArmorClass::Heavy), 0.5f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FExplosionParityTest, "CodexTactics.Weapons.Explosion.Parity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FExplosionParityTest::RunTest(const FString&)
{
	// The grenade (AGrenadeActor's old ApplyAreaEffect): Damage x GrenadeRules::DamageFalloff; the squad x0.65; bodies and hulls the same curve.
	const FExplosionSpec Grenade = Explosions::GrenadeSpec(FVector(10.f, 20.f, 30.f), 85.f, 400.f, TEXT("Grenade"), nullptr);
	TestTrue(TEXT("grenade: every actor in reach"), Grenade.Targets == EBlastTargets::AllActorsInRadius);
	TestTrue(TEXT("grenade: barrels / traps chain"), Grenade.bChainBarrelsAndTraps);
	TestTrue(TEXT("grenade: the squad's noise"), Grenade.bSquadNoise);
	TestFalse(TEXT("grenade: not a trap event"), Grenade.bTrapEvent);
	TestTrue(TEXT("grenade: other bodies plain (kinetic)"), Grenade.bOtherBodiesKinetic);
	TestTrue(TEXT("grenade: explosive"), Grenade.DamageType == EDamageType::Explosive);
	TestEqual(TEXT("grenade: no penetration"), Grenade.ArmorPenetration, 0.f);
	TestTrue(TEXT("grenade: the grenade event kind"), Grenade.Kind == GameplayEvents::ExplosionKinds::Grenade);
	TestFalse(TEXT("grenade: no cover rule (default config)"), ExplosionRules::Defaults().UsesOcclusion(Grenade.Kind));
	int32 Samples = 0;
	for (float Distance = 0.f; Distance <= 400.f; Distance += 12.5f)
	{
		const float Old = 85.f * GrenadeRules::DamageFalloff(Distance, 400.f);
		TestEqual(FString::Printf(TEXT("grenade enemy @%.1f"), Distance), ExplosionRules::VictimDamage(Grenade.EnemyDamage, Grenade.EnemyFalloff, Distance,
			Grenade.RadiusCm), Old);
		TestEqual(FString::Printf(TEXT("grenade squad @%.1f"), Distance), ExplosionRules::VictimDamage(Grenade.SquadDamage, Grenade.SquadFalloff, Distance,
			Grenade.RadiusCm, Grenade.SquadScale), Old * GrenadeRules::SquadDamageScale);
		++Samples;
	}

	// The placed charges (AInteractableActor's old ApplyBlast): DeployableRules::GetBlastDamage, enemies 0.4 / squad 0.5, at each caller's numbers.
	struct FCharge
	{
		const TCHAR* What;
		float Enemy;
		float Squad;
		float Radius;
		float Penetration;
		EDamageType Type;
	};
	const FCharge Charges[] = {
		{ TEXT("fuel barrel"), 120.f, 80.f, 550.f, 0.40f, EDamageType::Fire },
		{ TEXT("proximity mine"), 90.f, 90.f * DeployableRules::SquadDamageScale, 350.f, 0.50f, EDamageType::Explosive },
		{ TEXT("trapped object"), 85.f, 85.f * DeployableRules::SquadDamageScale, 400.f, 0.45f, EDamageType::Explosive },
	};
	for (const FCharge& Case : Charges)
	{
		const FExplosionSpec Spec = Explosions::ChargeSpec(FVector::ZeroVector, Case.Enemy, Case.Squad, Case.Radius, Case.Penetration, Case.Type,
			Case.What, nullptr);
		TestTrue(FString::Printf(TEXT("%s: enemies + the squad"), Case.What), Spec.Targets == EBlastTargets::EnemiesAndSquad);
		TestTrue(FString::Printf(TEXT("%s: a trap event"), Case.What), Spec.bTrapEvent);
		TestFalse(FString::Printf(TEXT("%s: no squad noise"), Case.What), Spec.bSquadNoise);
		TestTrue(FString::Printf(TEXT("%s: its damage type"), Case.What), Spec.DamageType == Case.Type);
		TestEqual(FString::Printf(TEXT("%s: its penetration"), Case.What), Spec.ArmorPenetration, Case.Penetration);
		TestEqual(FString::Printf(TEXT("%s: squad scale 1 (the base is already scaled)"), Case.What), Spec.SquadScale, 1.f);
		for (float Distance = 0.f; Distance <= Case.Radius + 50.f; Distance += 25.f)
		{
			TestEqual(FString::Printf(TEXT("%s enemy @%.0f"), Case.What, Distance),
				ExplosionRules::VictimDamage(Spec.EnemyDamage, Spec.EnemyFalloff, Distance, Spec.RadiusCm),
				DeployableRules::GetBlastDamage(Case.Enemy, Distance, Case.Radius, DeployableRules::EnemyFalloff));
			TestEqual(FString::Printf(TEXT("%s squad @%.0f"), Case.What, Distance),
				ExplosionRules::VictimDamage(Spec.SquadDamage, Spec.SquadFalloff, Distance, Spec.RadiusCm, Spec.SquadScale),
				DeployableRules::GetBlastDamage(Case.Squad, Distance, Case.Radius, DeployableRules::SquadFalloff));
			++Samples;
		}
	}
	TestTrue(TEXT("a charge with no source = the trap kind (the old GetExplosionKind)"),
		Explosions::ChargeSpec(FVector::ZeroVector, 1.f, 1.f, 1.f, 0.f, EDamageType::Explosive, TEXT("x"), nullptr).Kind == GameplayEvents::ExplosionKinds::Trap);
	AddInfo(FString::Printf(TEXT("%d distances compared bit-exact against the old formulas"), Samples));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FExplosionBackblastTest, "CodexTactics.Weapons.Explosion.Backblast",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FExplosionBackblastTest::RunTest(const FString&)
{
	// The RPG-7 (later): 8 m behind the shooter, 30 degrees each side.
	const FVector Launcher(100.f, 0.f, 150.f);
	const FVector Aim(1.f, 0.f, 0.f);
	TestEqual(TEXT("right behind: full"), ExplosionRules::BackblastMultiplier(Launcher, Aim, Launcher - FVector(1.f, 0.f, 0.f), 800.f, 30.f), 1.f, 0.01f);
	TestEqual(TEXT("4 m behind: half"), ExplosionRules::BackblastMultiplier(Launcher, Aim, Launcher - FVector(400.f, 0.f, 0.f), 800.f, 30.f), 0.5f, 1e-4f);
	TestEqual(TEXT("past 8 m: none"), ExplosionRules::BackblastMultiplier(Launcher, Aim, Launcher - FVector(801.f, 0.f, 0.f), 800.f, 30.f), 0.f);
	TestEqual(TEXT("in front: none"), ExplosionRules::BackblastMultiplier(Launcher, Aim, Launcher + FVector(200.f, 0.f, 0.f), 800.f, 30.f), 0.f);
	TestTrue(TEXT("20 degrees off the axis: inside the cone"),
		ExplosionRules::BackblastMultiplier(Launcher, Aim, Launcher + FVector(-300.f, 300.f * FMath::Tan(FMath::DegreesToRadians(20.f)), 0.f), 800.f, 30.f) > 0.f);
	TestEqual(TEXT("40 degrees off the axis: outside the cone"),
		ExplosionRules::BackblastMultiplier(Launcher, Aim, Launcher + FVector(-300.f, 300.f * FMath::Tan(FMath::DegreesToRadians(40.f)), 0.f), 800.f, 30.f), 0.f);
	TestEqual(TEXT("no back-blast (length 0)"), ExplosionRules::BackblastMultiplier(Launcher, Aim, Launcher - FVector(100.f, 0.f, 0.f), 0.f, 30.f), 0.f);

	TestEqual(TEXT("trauma 0 = no shake"), ExplosionRules::CameraTrauma(0.f, 0.f, 400.f), 0.f);
	TestEqual(TEXT("trauma at the blast = the base"), ExplosionRules::CameraTrauma(0.6f, 0.f, 400.f), 0.6f);
	TestEqual(TEXT("trauma at 2 radii = half"), ExplosionRules::CameraTrauma(0.6f, 800.f, 400.f), 0.3f, 1e-5f);
	TestEqual(TEXT("trauma past 4 radii = none"), ExplosionRules::CameraTrauma(0.6f, 1700.f, 400.f), 0.f);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
