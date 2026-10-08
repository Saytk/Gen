#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AbilitySystem/GenFeeding.h"
#include "AbilitySystem/GenIndicatorRules.h"
#include "AbilitySystem/GenMontageTiming.h"
#include "Champions/Curffe/CurffeHearthRules.h"
#include "Champions/Curffe/CurffeTuning.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGenMontageTimingTest, "Gen.Visuals.MontageTiming",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FGenMontageTimingTest::RunTest(const FString& Parameters)
{
	// Grande boule de feu : anticipation de 15 images (0.5 s) pour CastTime 0.5 s => vitesse 1
	TestEqual(TEXT("clip calé"), GenMontageTiming::GetPlayRate(0.5f, 0.5f), 1.f, 0.0001f);
	// Boule de feu : 11 images (0.3667 s) pour 0.35 s
	TestEqual(TEXT("léger écart"), GenMontageTiming::GetPlayRate(11.f / 30.f, 0.35f), (11.f / 30.f) / 0.35f, 0.0001f);
	// Nourrissage rapide (Combustion) : section de 0.3 s pour un intervalle de 0.15 s => x2, attendu
	TestEqual(TEXT("nourrissage rapide"), GenMontageTiming::GetPlayRate(0.3f, 0.15f), 2.f, 0.0001f);
	TestFalse(TEXT("x2 attendu : pas d'avertissement"), GenMontageTiming::ShouldWarn(2.f, 2.f));
	TestTrue(TEXT("x1.5 non attendu : avertissement"), GenMontageTiming::ShouldWarn(1.5f, 1.f));
	// Bornes
	TestEqual(TEXT("borne haute"), GenMontageTiming::GetPlayRate(1.f, 0.1f), GenMontageTiming::MaxPlayRate, 0.0001f);
	TestEqual(TEXT("borne basse"), GenMontageTiming::GetPlayRate(0.1f, 1.f), GenMontageTiming::MinPlayRate, 0.0001f);
	TestEqual(TEXT("durée nulle : vitesse 1"), GenMontageTiming::GetPlayRate(0.5f, 0.f), 1.f, 0.0001f);
	TestEqual(TEXT("clip vide : vitesse 1"), GenMontageTiming::GetPlayRate(0.f, 0.5f), 1.f, 0.0001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGenMontagePhaseRatesTest, "Gen.Visuals.MontagePhaseRates",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FGenMontagePhaseRatesTest::RunTest(const FString& Parameters)
{
	// Feed : section Feed_1 d'un intervalle de base (0.3 s), jouée sur l'intervalle actif figé au début du nourrissage
	const float Base = CurffeTuning::FeedInterval;
	const float Section = Base;
	for (const bool bFast : { false, true })
	{
		const float Active = GenFeeding::GetFeedInterval(Base, bFast);
		const float Rate = GenMontageTiming::GetPlayRate(Section, Active);
		const float Expected = GenMontageTiming::GetExpectedFeedRate(Base, Active);
		const TCHAR* Mode = bFast ? TEXT("rapide") : TEXT("normal");

		TestEqual(FString::Printf(TEXT("%s : vitesse"), Mode), Rate, bFast ? 2.f : 1.f, 0.0001f);
		TestEqual(FString::Printf(TEXT("%s : vitesse attendue"), Mode), Expected, Rate, 0.0001f);
		TestFalse(FString::Printf(TEXT("%s : pas d'avertissement"), Mode), GenMontageTiming::ShouldWarn(Rate, Expected));

		// Les frontières de section tombent sur les seuils, N = 0..MaxFeed : l'anticipation ne dépasse jamais le nourrissage
		for (int32 N = 0; N <= CurffeTuning::MaxFeedPerSpell; ++N)
		{
			TestEqual(FString::Printf(TEXT("%s : fin de Feed_%d au seuil %d"), Mode, N, N), N * Section / Rate, N * Active, 0.0001f);
		}
	}
	TestEqual(TEXT("seuils rapides = CurffeTuning::FastFeedInterval"), GenFeeding::GetFeedInterval(Base, true), CurffeTuning::FastFeedInterval, 0.0001f);
	TestEqual(TEXT("intervalle nul : vitesse attendue 1"), GenMontageTiming::GetExpectedFeedRate(Base, 0.f), 1.f, 0.0001f);

	// Charge : calée sur CastTime seulement avec un CastMontage à part (sinon montage unique du Plan 1, vitesse 1)
	TestTrue(TEXT("phases séparées : calée"), GenMontageTiming::ShouldScaleChargeToCastTime(true, true, 0.5f));
	TestFalse(TEXT("montage unique : vitesse 1"), GenMontageTiming::ShouldScaleChargeToCastTime(true, false, 0.5f));
	TestFalse(TEXT("désactivé par le sort"), GenMontageTiming::ShouldScaleChargeToCastTime(false, true, 0.5f));
	TestFalse(TEXT("sort instantané"), GenMontageTiming::ShouldScaleChargeToCastTime(true, true, 0.f));
	// Charge de 0.45 s pour la grande boule de feu (0.5 s) : x0.9, sans avertissement
	const float ChargeRate = GenMontageTiming::GetPlayRate(0.45f, 0.5f);
	TestEqual(TEXT("charge : longueur / CastTime"), ChargeRate, 0.9f, 0.0001f);
	TestEqual(TEXT("charge : finit au lancer"), 0.45f / ChargeRate, 0.5f, 0.0001f);
	TestFalse(TEXT("charge x0.9 : pas d'avertissement"), GenMontageTiming::ShouldWarn(ChargeRate, 1.f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGenProjectileAimTest, "Gen.Visuals.ProjectileAim",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FGenProjectileAimTest::RunTest(const FString& Parameters)
{
	GenIndicatorRules::FProjectileAimParams P;
	P.SpawnForwardOffset = 70.f;
	P.Range = 1300.f;
	P.CollisionRadius = 20.f;
	P.ScaleAtMaxFeed = 2.f;
	P.MaxFeed = 3;
	P.ExplosionMinFeed = 2;
	P.ExplosionRadius = 150.f;
	P.KnockbackMinFeed = 3;

	FGenAimGeometry G;
	GenIndicatorRules::ComputeProjectileAim(FVector::ZeroVector, FVector(1.f, 0.f, 0.f), P, 0, -1.f, G);
	TestEqual(TEXT("départ au point d'apparition"), G.LineStart.X, 70.0, 0.01);
	TestEqual(TEXT("longueur = portée"), G.LineLength, 1300.f, 0.01f);
	TestEqual(TEXT("largeur = diamètre de collision"), G.LineWidth, 40.f, 0.01f);
	TestEqual(TEXT("0 flamme : pas d'éclat"), G.CapRadius, 0.f);

	GenIndicatorRules::ComputeProjectileAim(FVector::ZeroVector, FVector(1.f, 0.f, 0.f), P, 1, -1.f, G);
	TestEqual(TEXT("1 flamme : largeur x ScaleByFeed"), G.LineWidth, 40.f * GenFeeding::ScaleByFeed(1.f, 2.f, 1, 3), 0.01f);
	TestEqual(TEXT("1 flamme : pas d'éclat"), G.CapRadius, 0.f);

	GenIndicatorRules::ComputeProjectileAim(FVector::ZeroVector, FVector(1.f, 0.f, 0.f), P, 2, -1.f, G);
	TestEqual(TEXT("2 flammes : éclat 150"), G.CapRadius, 150.f, 0.01f);
	TestFalse(TEXT("2 flammes : pas de rayons d'expulsion"), G.bCapSpokes);
	TestEqual(TEXT("éclat au bout de la portée"), G.CapCenter.X, 70.0 + 1300.0, 0.01);

	GenIndicatorRules::ComputeProjectileAim(FVector::ZeroVector, FVector(1.f, 0.f, 0.f), P, 3, -1.f, G);
	TestTrue(TEXT("3 flammes : rayons d'expulsion"), G.bCapSpokes);
	TestEqual(TEXT("3 flammes : largeur max"), G.LineWidth, 80.f, 0.01f);

	// Mur à 500 cm du lanceur : la ligne s'arrête au mur, l'éclat est posé contre le mur (centre à un rayon du mur)
	GenIndicatorRules::ComputeProjectileAim(FVector::ZeroVector, FVector(1.f, 0.f, 0.f), P, 3, 500.f, G);
	TestEqual(TEXT("ligne coupée au mur"), G.LineLength, 430.f, 0.01f);
	TestEqual(TEXT("éclat contre le mur"), G.CapCenter.X, 500.0 - 40.0, 0.01);

	// Direction non horizontale ou nulle : aplatie, sinon avant du monde
	GenIndicatorRules::ComputeProjectileAim(FVector::ZeroVector, FVector::ZeroVector, P, 0, -1.f, G);
	TestTrue(TEXT("direction nulle : X"), G.Direction.Equals(FVector(1.f, 0.f, 0.f), 0.001f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGenLeapAimTest, "Gen.Visuals.LeapAim",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FGenLeapAimTest::RunTest(const FString& Parameters)
{
	GenIndicatorRules::FLeapAimParams P;
	P.MaxDistance = 700.f;
	P.LandingRadius = 150.f;
	P.StubLength = 150.f;
	P.RingProjectileRadius = 20.f;

	FGenAimGeometry G;
	GenIndicatorRules::ComputeLeapAim(FVector::ZeroVector, FVector(1500.f, 0.f, 0.f), P, 3, G);
	TestEqual(TEXT("atterrissage borné à 7 m"), G.TargetCenter.X, 700.0, 0.01);
	TestEqual(TEXT("rayon d'atterrissage"), G.TargetRadius, 150.f, 0.01f);
	TestEqual(TEXT("arc de portée"), G.RangeArcRadius, 700.f, 0.01f);
	TestEqual(TEXT("3 flammes : 3 amorces"), G.StubDirections.Num(), 3);
	// Mêmes directions que l'anneau réel (UCurffeGA_MeteorLeap : GetRingDirections(Fed, AimDirection))
	const TArray<FVector> Ring = GenAreaRules::GetRingDirections(3, FVector(1.f, 0.f, 0.f));
	for (int32 Index = 0; Index < 3 && Index < G.StubDirections.Num(); ++Index)
	{
		TestTrue(*FString::Printf(TEXT("amorce %d = direction de l'anneau"), Index), G.StubDirections[Index].Equals(Ring[Index], 0.001f));
	}
	TestEqual(TEXT("largeur d'amorce = diamètre de la boule de feu"), G.StubWidth, 40.f, 0.01f);

	GenIndicatorRules::ComputeLeapAim(FVector::ZeroVector, FVector(300.f, 400.f, 0.f), P, 0, G);
	TestEqual(TEXT("curseur dans la portée : atterrissage au curseur"), G.TargetCenter.Y, 400.0, 0.01);
	TestEqual(TEXT("0 flamme : aucune amorce"), G.StubDirections.Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGenIndicatorLayerTest, "Gen.Visuals.ThresholdPop",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FGenIndicatorLayerTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("0 -> 1 : seuil"), GenIndicatorRules::IsThresholdPop(0, 1));
	TestTrue(TEXT("2 -> 3 : seuil"), GenIndicatorRules::IsThresholdPop(2, 3));
	TestTrue(TEXT("correction 1 -> 3 : un seul seuil annoncé"), GenIndicatorRules::IsThresholdPop(1, 3));
	TestFalse(TEXT("lancer 3 -> 0 : rien"), GenIndicatorRules::IsThresholdPop(3, 0));
	TestFalse(TEXT("correction 2 -> 1 : rien"), GenIndicatorRules::IsThresholdPop(2, 1));
	TestFalse(TEXT("inchangé : rien"), GenIndicatorRules::IsThresholdPop(2, 2));

	// Art Bible §7.5 : ennemi au-dessus de tout ; sa propre visée au-dessus des alliés
	using GenIndicatorRules::GetSortPriority;
	TestTrue(TEXT("ennemi > soi"), GetSortPriority(EGenViewerRelation::Enemy) > GetSortPriority(EGenViewerRelation::Self));
	TestTrue(TEXT("soi > allié"), GetSortPriority(EGenViewerRelation::Self) > GetSortPriority(EGenViewerRelation::Ally));
	TestEqual(TEXT("neutre = allié"), GetSortPriority(EGenViewerRelation::Neutral), GetSortPriority(EGenViewerRelation::Ally));
	TestTrue(TEXT("marqueur de projectile au-dessus de tout"), GenIndicatorRules::GroundMarkerSortPriority > GetSortPriority(EGenViewerRelation::Enemy));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCurffeHearthSocketsTest, "Gen.Curffe.HearthSockets",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCurffeHearthSocketsTest::RunTest(const FString& Parameters)
{
	using CurffeHearthRules::ESocket;
	TArray<ESocket, TInlineAllocator<8>> S;

	CurffeHearthRules::GetSocketStates(5, 0, 5, S);
	TestEqual(TEXT("Foyer plein : 5 allumées"), S.FilterByPredicate([](ESocket X) { return X == ESocket::Lit; }).Num(), 5);

	CurffeHearthRules::GetSocketStates(5, 2, 5, S);
	TestTrue(TEXT("2 nourries : 3 allumées puis 2 parties"), S[2] == ESocket::Lit && S[3] == ESocket::InSpell && S[4] == ESocket::InSpell);

	CurffeHearthRules::GetSocketStates(0, 0, 5, S);
	TestEqual(TEXT("mage vide : 5 emplacements toujours là"), S.Num(), 5);
	TestTrue(TEXT("mage vide : tous éteints"), S[0] == ESocket::Empty && S[4] == ESocket::Empty);

	CurffeHearthRules::GetSocketStates(2, 3, 5, S);
	TestTrue(TEXT("compte nourri au-delà des flammes (correction) : borné"), S[0] == ESocket::InSpell && S[1] == ESocket::InSpell && S[2] == ESocket::Empty);

	CurffeHearthRules::GetSocketStates(9, 0, 5, S);
	TestEqual(TEXT("plus de flammes que d'emplacements : borné"), S.Num(), 5);
	return true;
}

#endif
