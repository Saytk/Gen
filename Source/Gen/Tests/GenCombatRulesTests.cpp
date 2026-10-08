#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AbilitySystem/GenAreaRules.h"
#include "AbilitySystem/GenFeeding.h"
#include "AbilitySystem/GenHitRules.h"
#include "AbilitySystem/GenSalvo.h"
#include "UObject/Class.h"
#include "UObject/Object.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGenCounterTriggerTest, "Gen.Combat.CounterTrigger",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FGenCounterTriggerTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("un projectile déclenche le contre"), GenHitRules::TriggersCounter(EGenHitKind::Projectile));
	TestTrue(TEXT("la mêlée déclenche le contre"), GenHitRules::TriggersCounter(EGenHitKind::Melee));
	TestFalse(TEXT("une zone au sol traverse le contre"), GenHitRules::TriggersCounter(EGenHitKind::Area));

	TestTrue(TEXT("projectile sur un contre : bloqué"), GenHitRules::Resolve(true, EGenHitKind::Projectile) == EGenHitResponse::Countered);
	TestTrue(TEXT("zone sur un contre : touché"), GenHitRules::Resolve(true, EGenHitKind::Area) == EGenHitResponse::Hit);
	TestTrue(TEXT("projectile sans contre : touché"), GenHitRules::Resolve(false, EGenHitKind::Projectile) == EGenHitResponse::Hit);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGenCounterRewardTest, "Gen.Combat.CounterReward",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FGenCounterRewardTest::RunTest(const FString& Parameters)
{
	// GetCounterReward(BlockIndex, ResourcePerBlock, EnergyOnFirstBlock)
	const GenHitRules::FCounterReward First = GenHitRules::GetCounterReward(1, 2.f, 10.f);
	TestEqual(TEXT("1er blocage : +2 flammes"), First.Resource, 2.f);
	TestEqual(TEXT("1er blocage : +10 énergie"), First.Energy, 10.f);

	const GenHitRules::FCounterReward Second = GenHitRules::GetCounterReward(2, 2.f, 10.f);
	TestEqual(TEXT("2e blocage : +2 flammes"), Second.Resource, 2.f);
	TestEqual(TEXT("2e blocage : énergie une seule fois par incantation"), Second.Energy, 0.f);

	const GenHitRules::FCounterReward None = GenHitRules::GetCounterReward(0, 2.f, 10.f);
	TestEqual(TEXT("aucun blocage : rien"), None.Resource + None.Energy, 0.f);

	const GenHitRules::FCounterReward Negative = GenHitRules::GetCounterReward(1, -2.f, -10.f);
	TestEqual(TEXT("valeurs négatives ignorées"), Negative.Resource + Negative.Energy, 0.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGenClampToRangeTest, "Gen.Area.ClampToRange",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FGenClampToRangeTest::RunTest(const FString& Parameters)
{
	const FVector Origin(0.f, 0.f, 0.f);
	const FVector Target(300.f, 400.f, 10.f); // à 500 cm
	TestEqual(TEXT("dans la portée : inchangé"), GenAreaRules::ClampToRange(Origin, Target, 900.f), Target, 0.01f);
	TestEqual(TEXT("ramené à 250 cm, Z conservé"), GenAreaRules::ClampToRange(Origin, Target, 250.f), FVector(150.f, 200.f, 10.f), 0.01f);
	TestEqual(TEXT("portée nulle : sur le lanceur"), GenAreaRules::ClampToRange(Origin, Target, 0.f), FVector(0.f, 0.f, 10.f), 0.01f);
	TestEqual(TEXT("cible sur le lanceur"), GenAreaRules::ClampToRange(Origin, FVector(0.f, 0.f, 5.f), 900.f), FVector(0.f, 0.f, 5.f), 0.01f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGenRingDirectionsTest, "Gen.Area.RingDirections",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FGenRingDirectionsTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("0 flamme : pas d'anneau"), GenAreaRules::GetRingDirections(0, FVector::ForwardVector).Num(), 0);

	const TArray<FVector> One = GenAreaRules::GetRingDirections(1, FVector(0.f, 1.f, 0.f));
	TestEqual(TEXT("1 flamme : une direction"), One.Num(), 1);
	TestEqual(TEXT("1 flamme : droit devant"), One[0], FVector(0.f, 1.f, 0.f), 0.001f);

	// Anneaux réguliers de 2 à 5 branches : le plafond de nourrissage (MaxFeed du sort) n'est pas une règle de l'anneau
	for (int32 Count = 2; Count <= 5; ++Count)
	{
		const TArray<FVector> Ring = GenAreaRules::GetRingDirections(Count, FVector(1.f, 0.f, 0.5f));
		TestEqual(FString::Printf(TEXT("%d branches"), Count), Ring.Num(), Count);
		TestEqual(TEXT("première branche selon l'avant aplati"), Ring[0], FVector(1.f, 0.f, 0.f), 0.001f);
		const float ExpectedCos = FMath::Cos(FMath::DegreesToRadians(360.f / Count));
		FVector Sum = FVector::ZeroVector;
		for (int32 Index = 0; Index < Ring.Num(); ++Index)
		{
			TestEqual(TEXT("direction unitaire"), static_cast<float>(Ring[Index].Size()), 1.f, 0.001f);
			TestEqual(TEXT("horizontale"), static_cast<float>(Ring[Index].Z), 0.f, 0.001f);
			const FVector& Next = Ring[(Index + 1) % Ring.Num()];
			TestEqual(TEXT("360° / Count entre deux branches"), static_cast<float>(FVector::DotProduct(Ring[Index], Next)), ExpectedCos, 0.001f);
			Sum += Ring[Index];
		}
		TestEqual(TEXT("anneau régulier (somme nulle)"), Sum, FVector::ZeroVector, 0.001f);
	}

	TestEqual(TEXT("avant nul : axe X"), GenAreaRules::GetRingDirections(1, FVector::ZeroVector)[0], FVector(1.f, 0.f, 0.f), 0.001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGenLineOfSightSamplesTest, "Gen.Area.LineOfSightSamples",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FGenLineOfSightSamplesTest::RunTest(const FString& Parameters)
{
	// Cible à +X : les bords sont sur l'axe Y (perpendiculaires à la ligne de vue)
	const TArray<FVector> Samples = GenAreaRules::GetLineOfSightSamples(FVector::ZeroVector, FVector(500.f, 0.f, 0.f), 40.f, 90.f);
	TestEqual(TEXT("4 points"), Samples.Num(), 4);
	TestEqual(TEXT("centre"), Samples[0], FVector(500.f, 0.f, 0.f), 0.01f);
	TestEqual(TEXT("bord gauche"), Samples[1], FVector(500.f, 40.f, 0.f), 0.01f);
	TestEqual(TEXT("bord droit"), Samples[2], FVector(500.f, -40.f, 0.f), 0.01f);
	TestEqual(TEXT("haut"), Samples[3], FVector(500.f, 0.f, 90.f), 0.01f);

	const TArray<FVector> SameSpot = GenAreaRules::GetLineOfSightSamples(FVector::ZeroVector, FVector(0.f, 0.f, 50.f), 40.f, 90.f);
	TestEqual(TEXT("cible à la verticale de l'origine : bords sur Y"), SameSpot[1], FVector(0.f, 40.f, 50.f), 0.01f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGenViewerRelationTest, "Gen.Area.ViewerRelation",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FGenViewerRelationTest::RunTest(const FString& Parameters)
{
	// GetViewerRelation(bViewerIsSource, ViewerTeam, SourceTeam, NoTeam)
	TestTrue(TEXT("son propre sort"), GenAreaRules::GetViewerRelation(true, 0, 0, 255) == EGenViewerRelation::Self);
	TestTrue(TEXT("sort d'un allié"), GenAreaRules::GetViewerRelation(false, 0, 0, 255) == EGenViewerRelation::Ally);
	TestTrue(TEXT("sort d'un ennemi"), GenAreaRules::GetViewerRelation(false, 0, 1, 255) == EGenViewerRelation::Enemy);
	TestTrue(TEXT("sort neutre (mannequin)"), GenAreaRules::GetViewerRelation(false, 0, 255, 255) == EGenViewerRelation::Neutral);
	TestTrue(TEXT("spectateur sans équipe : ennemi"), GenAreaRules::GetViewerRelation(false, 255, 1, 255) == EGenViewerRelation::Enemy);
	TestEqual(TEXT("valeurs = RelationIndex du matériau"), static_cast<int32>(EGenViewerRelation::Enemy), 3);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGenTelegraphTimingTest, "Gen.Area.TelegraphTiming",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FGenTelegraphTimingTest::RunTest(const FString& Parameters)
{
	// GetImpactDelay(Delay, MinTelegraph) ; minimum 0.6 s (guidelines §3.1 : zones retardées 0.6–1.0 s)
	TestEqual(TEXT("zone instantanée"), GenAreaRules::GetImpactDelay(0.f, 0.6f), 0.f);
	TestEqual(TEXT("pilier : 0.8 s"), GenAreaRules::GetImpactDelay(0.8f, 0.6f), 0.8f);
	TestEqual(TEXT("jamais sous le minimum"), GenAreaRules::GetImpactDelay(0.3f, 0.6f), 0.6f);

	// GetTelegraphFill(Elapsed, Delay)
	TestEqual(TEXT("mi-parcours"), GenAreaRules::GetTelegraphFill(0.4f, 0.8f), 0.5f, 0.001f);
	TestEqual(TEXT("avant le début"), GenAreaRules::GetTelegraphFill(-1.f, 0.8f), 0.f);
	TestEqual(TEXT("après l'impact"), GenAreaRules::GetTelegraphFill(2.f, 0.8f), 1.f);
	TestEqual(TEXT("sans délai"), GenAreaRules::GetTelegraphFill(0.f, 0.f), 1.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGenSalvoTest, "Gen.Combat.Salvo",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FGenSalvoTest::RunTest(const FString& Parameters)
{
	// Deux objets distincts quelconques jouent le rôle de deux ennemis
	const UObject* EnemyA = GetDefault<UObject>();
	const UObject* EnemyB = UObject::StaticClass();

	FGenProjectileSalvo Salvo;
	TestFalse(TEXT("personne n'est touché au départ"), Salvo.HasHit(EnemyA));
	TestTrue(TEXT("premier projectile sur A"), Salvo.TryClaim(EnemyA));
	TestFalse(TEXT("deuxième projectile sur A : traverse"), Salvo.TryClaim(EnemyA));
	TestTrue(TEXT("A est marqué"), Salvo.HasHit(EnemyA));
	TestFalse(TEXT("B reste touchable"), Salvo.HasHit(EnemyB));
	TestTrue(TEXT("premier projectile sur B"), Salvo.TryClaim(EnemyB));
	TestFalse(TEXT("cible nulle"), Salvo.TryClaim(nullptr));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGenFedDisplayTest, "Gen.Feeding.FedDisplay",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FGenFedDisplayTest::RunTest(const FString& Parameters)
{
	const FObjectKey GreatFireball(GetDefault<UObject>());
	const FObjectKey Leap(UObject::StaticClass());

	GenFeeding::FFedDisplay Display;
	TestTrue(TEXT("la grosse boule de feu affiche 3"), Display.Set(GreatFireball, 3));
	TestEqual(TEXT("3 affichées"), static_cast<int32>(Display.Count), 3);
	TestFalse(TEXT("le bond, qui n'affichait rien, ne l'efface pas"), Display.Set(Leap, 0));
	TestEqual(TEXT("toujours 3"), static_cast<int32>(Display.Count), 3);
	TestTrue(TEXT("son propriétaire l'efface"), Display.Set(GreatFireball, 0));
	TestEqual(TEXT("0 affichée"), static_cast<int32>(Display.Count), 0);
	TestTrue(TEXT("le bond prend la main"), Display.Set(Leap, 2));
	TestTrue(TEXT("un nouveau sort qui nourrit remplace l'affichage"), Display.Set(GreatFireball, 1));
	TestFalse(TEXT("l'ancien ne l'efface plus"), Display.Set(Leap, 0));
	TestEqual(TEXT("1 affichée"), static_cast<int32>(Display.Count), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGenCastLockRuleTest, "Gen.Feeding.CastLockRule",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FGenCastLockRuleTest::RunTest(const FString& Parameters)
{
	// GetCastLockEnforcedUntil(LockStart, LockDuration, Tolerance)
	TestEqual(TEXT("bond de 0.45 s lancé à 10 s : refus serveur jusqu'à 10.35 s"), GenFeeding::GetCastLockEnforcedUntil(10.f, 0.45f, 0.1f), 10.35f, 0.0001f);
	TestEqual(TEXT("verrou plus court que la tolérance : aucun refus serveur"), GenFeeding::GetCastLockEnforcedUntil(10.f, 0.05f, 0.1f), 10.f, 0.0001f);

	// IsRefusedByCastLock(bLocked, bPredictingSide, Now, EnforcedUntil)
	TestFalse(TEXT("pas de verrou"), GenFeeding::IsRefusedByCastLock(false, true, 10.f, 11.f));
	TestTrue(TEXT("client : son propre verrou fait foi"), GenFeeding::IsRefusedByCastLock(true, true, 20.f, 10.35f));
	TestTrue(TEXT("serveur, tôt dans le verrou : refusé (triche)"), GenFeeding::IsRefusedByCastLock(true, false, 10.2f, 10.35f));
	TestFalse(TEXT("serveur, fin du vol : le client a déjà atterri, accepté"), GenFeeding::IsRefusedByCastLock(true, false, 10.4f, 10.35f));
	return true;
}

#endif
