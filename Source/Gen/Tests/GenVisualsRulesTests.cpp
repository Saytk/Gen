#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AbilitySystem/GenFeeding.h"
#include "AbilitySystem/GenIndicatorRules.h"
#include "AbilitySystem/GenMontageTiming.h"
#include "Champions/Curffe/CurffeHearthRules.h"
#include "Champions/Curffe/CurffeTuning.h"
#include "Actors/GenProjectile.h"
#include "UObject/UnrealType.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGenFedChargeSectionTest, "Gen.Visuals.FedChargeSection",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FGenFedChargeSectionTest::RunTest(const FString& Parameters)
{
	// UGenGA_Cast::FedChargeMontage (AM_FlamePillar_Charge_Fed : Fed_1@0, Fed_2@0.4, Fed_3@0.8, 0.4 s chacune)
	TestEqual(TEXT("sans nourrissage : ChargeMontage"), GenMontageTiming::GetFedChargeSection(0), FName(NAME_None));
	TestEqual(TEXT("1 unité"), GenMontageTiming::GetFedChargeSection(1), FName(TEXT("Fed_1")));
	TestEqual(TEXT("2 unités"), GenMontageTiming::GetFedChargeSection(2), FName(TEXT("Fed_2")));
	TestEqual(TEXT("3 unités"), GenMontageTiming::GetFedChargeSection(3), FName(TEXT("Fed_3")));
	TestEqual(TEXT("au-delà : bornée à Fed_3"), GenMontageTiming::GetFedChargeSection(5), FName(TEXT("Fed_3")));
	// Longueur de la SECTION (0.4 s), jamais celle du montage (1.2 s) : calée sur CastTime
	TestEqual(TEXT("0.4 s sur 0.4 s : vitesse 1"), GenMontageTiming::GetFedChargeRate(0.4f, 0.4f), 1.f, 0.0001f);
	TestEqual(TEXT("0.4 s sur 0.5 s : 0.8"), GenMontageTiming::GetFedChargeRate(0.4f, 0.5f), 0.8f, 0.0001f);
	TestTrue(TEXT("pas la longueur du montage"), !FMath::IsNearlyEqual(GenMontageTiming::GetFedChargeRate(0.4f, 0.5f), GenMontageTiming::GetPlayRate(1.2f, 0.5f)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGenMontageTimingTest, "Gen.Visuals.MontageTiming",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FGenMontageTimingTest::RunTest(const FString& Parameters)
{
	// Grande boule de feu : anticipation de 15 images (0.5 s) pour CastTime 0.5 s => vitesse 1
	TestEqual(TEXT("clip calé"), GenMontageTiming::GetPlayRate(0.5f, 0.5f), 1.f, 0.0001f);
	// Boule de feu : 11 images (0.3667 s) pour 0.40 s
	TestEqual(TEXT("léger écart"), GenMontageTiming::GetPlayRate(11.f / 30.f, 0.40f), (11.f / 30.f) / 0.40f, 0.0001f);
	// Nourrissage rapide (Combustion) : section de 0.3 s pour un intervalle de 0.15 s => x2, attendu
	TestEqual(TEXT("nourrissage rapide"), GenMontageTiming::GetPlayRate(0.3f, 0.15f), 2.f, 0.0001f);
	TestFalse(TEXT("x2 attendu : pas d'avertissement"), GenMontageTiming::ShouldWarn(2.f, 2.f));
	TestTrue(TEXT("x1.5 non attendu : avertissement"), GenMontageTiming::ShouldWarn(1.5f, 1.f));
	// Bornes
	TestEqual(TEXT("borne haute"), GenMontageTiming::GetPlayRate(1.f, 0.1f), GenMontageTiming::MaxPlayRate, 0.0001f);
	TestEqual(TEXT("borne basse"), GenMontageTiming::GetPlayRate(0.1f, 1.f), GenMontageTiming::MinPlayRate, 0.0001f);
	TestEqual(TEXT("durée nulle : vitesse 1"), GenMontageTiming::GetPlayRate(0.5f, 0.f), 1.f, 0.0001f);
	TestEqual(TEXT("clip vide : vitesse 1"), GenMontageTiming::GetPlayRate(0.f, 0.5f), 1.f, 0.0001f);

	// PIE : avec 2 flammes, le lanceur ne joue jamais Feed_3 (section tenue = dernier seuil atteignable)
	TestEqual(TEXT("2 flammes sur 3 sections : Feed_2 tenue"), GenMontageTiming::GetFeedHoldSection(2, 3), 2);
	TestTrue(TEXT("2 flammes : Feed_2 figée à sa fin"), GenMontageTiming::ShouldHoldFeedSection(2, 3));
	TestEqual(TEXT("1 flamme : Feed_1 tenue"), GenMontageTiming::GetFeedHoldSection(1, 3), 1);
	TestTrue(TEXT("1 flamme : Feed_1 figée à sa fin"), GenMontageTiming::ShouldHoldFeedSection(1, 3));
	TestFalse(TEXT("3 flammes : Feed_3 enchaîne sur Feed_3_Hold (asset), rien à figer"), GenMontageTiming::ShouldHoldFeedSection(3, 3));
	TestEqual(TEXT("plus de flammes que de sections : la dernière"), GenMontageTiming::GetFeedHoldSection(5, 3), 3);
	TestFalse(TEXT("plus de flammes que de sections : rien à figer"), GenMontageTiming::ShouldHoldFeedSection(5, 3));
	TestFalse(TEXT("rien à nourrir : rien à tenir"), GenMontageTiming::ShouldHoldFeedSection(0, 3));
	TestFalse(TEXT("montage sans section Feed_N"), GenMontageTiming::ShouldHoldFeedSection(2, 0));

	// AM_Curffe_FeedHand : Feed_1 0-0.3, Feed_2 0.3-0.6, Feed_3 0.6-0.9, Feed_3_Hold 0.9-1.5 (boucle). La tenue n'est
	// pas un seuil : 3 sections comptées, et un plafond à 3 ne fige rien
	const TSet<FName> FeedHand = { TEXT("Feed_1"), TEXT("Feed_2"), TEXT("Feed_3"), TEXT("Feed_3_Hold") };
	const int32 FeedHandCount = GenMontageTiming::CountFeedSections([&FeedHand](FName Section) { return FeedHand.Contains(Section); });
	TestEqual(TEXT("Feed_3_Hold ne compte pas comme un seuil"), FeedHandCount, 3);
	TestFalse(TEXT("3 flammes avec Feed_3_Hold : rien à figer"), GenMontageTiming::ShouldHoldFeedSection(3, FeedHandCount));
	TestEqual(TEXT("2 flammes avec Feed_3_Hold : Feed_2"), GenMontageTiming::GetFeedSectionName(GenMontageTiming::GetFeedHoldSection(2, FeedHandCount)), FName(TEXT("Feed_2")));
	const TSet<FName> Gap = { TEXT("Feed_1"), TEXT("Feed_3") };
	TestEqual(TEXT("sections consécutives seulement"), GenMontageTiming::CountFeedSections([&Gap](FName Section) { return Gap.Contains(Section); }), 1);
	TestEqual(TEXT("comptage borné"), GenMontageTiming::CountFeedSections([](FName) { return true; }), GenMontageTiming::MaxFeedSections);

	// Gel à la fin de Feed_2 (0.6) depuis le début du geste, vitesse 1, images de 1/60 s : deux images d'avance
	TestEqual(TEXT("Feed_2 : gel deux images avant la fin"), GenMontageTiming::GetFeedHoldDelay(0.6f, 0.f, 1.f, 1.f / 60.f), 0.6f - 2.f / 60.f, 0.0001f);
	// Nourrissage rapide (x2) : 0.3 s de jeu jusqu'à la fin de Feed_2
	TestEqual(TEXT("x2 : gel avant 0.3 s"), GenMontageTiming::GetFeedHoldDelay(0.6f, 0.f, 2.f, 1.f / 60.f), 0.3f - 2.f / 60.f, 0.0001f);
	// Le geste figé ne franchit jamais la frontière : délai + deux images <= temps restant
	TestTrue(TEXT("jamais au-delà de la fin"), GenMontageTiming::GetFeedHoldDelay(0.3f, 0.f, 1.f, 1.f / 30.f) + 2.f / 30.f <= 0.3f + 0.0001f);
	TestEqual(TEXT("marge bornée à la moitié du temps restant"), GenMontageTiming::GetFeedHoldDelay(0.3f, 0.25f, 1.f, 0.1f), 0.025f, 0.0001f);
	TestEqual(TEXT("fin déjà atteinte : gel immédiat"), GenMontageTiming::GetFeedHoldDelay(0.6f, 0.65f, 1.f, 1.f / 60.f), 0.f, 0.0001f);
	TestEqual(TEXT("geste à l'arrêt : rien à figer"), GenMontageTiming::GetFeedHoldDelay(0.6f, 0.f, 0.f, 1.f / 60.f), -1.f, 0.0001f);

	// PIE : clic gauche maintenu, la charge suivante ne coupe pas le geste de lancer (0.15 s protégées)
	TestEqual(TEXT("une image après le lancer : charge retardée"), GenMontageTiming::GetChargeStartDelay(0.016f, 0.4f, 0.15f), 0.134f, 0.0001f);
	TestEqual(TEXT("geste fini depuis longtemps : pas de retard"), GenMontageTiming::GetChargeStartDelay(0.3f, 0.4f, 0.15f), 0.f, 0.0001f);
	TestEqual(TEXT("aucun geste en cours : pas de retard"), GenMontageTiming::GetChargeStartDelay(-1.f, 0.4f, 0.15f), 0.f, 0.0001f);
	TestEqual(TEXT("retard borné à la moitié de l'incantation"), GenMontageTiming::GetChargeStartDelay(0.f, 0.2f, 0.15f), 0.1f, 0.0001f);
	TestEqual(TEXT("sans protection : pas de retard"), GenMontageTiming::GetChargeStartDelay(0.f, 0.4f, 0.f), 0.f, 0.0001f);
	// La charge retardée finit toujours à CastTime : clip de 0.4 s, retard 0.134 s => joué sur 0.266 s
	const float Delay = GenMontageTiming::GetChargeStartDelay(0.016f, 0.4f, 0.15f);
	const float Rate = GenMontageTiming::GetDelayedChargeRate(0.4f, 0.4f, Delay);
	TestEqual(TEXT("charge retardée : finit à CastTime"), Delay + 0.4f / Rate, 0.4f, 0.0001f);
	TestEqual(TEXT("sans retard : vitesse normale"), GenMontageTiming::GetDelayedChargeRate(0.4f, 0.4f, 0.f), 1.f, 0.0001f);
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

	// Revue V2-V4 : Feed_1 mal calé (0.4 s au lieu de 0.3 s) : la vitesse rattrape (x1.33), mais il faut avertir
	{
		const float Rate = GenMontageTiming::GetPlayRate(0.4f, Base);
		TestEqual(TEXT("mal calé : vitesse longueur / intervalle"), Rate, 0.4f / Base, 0.0001f);
		TestTrue(TEXT("mal calé : avertissement"), GenMontageTiming::ShouldWarn(Rate, GenMontageTiming::GetExpectedFeedRate(Base, Base)));
		TestEqual(TEXT("mal calé mais non borné : la 1re frontière tombe encore au seuil"), 0.4f / Rate, Base, 0.0001f);
	}
	// Feed_1 beaucoup trop long (1 s) : vitesse bornée à MaxPlayRate, les frontières dépassent les seuils, avertissement
	{
		const float Rate = GenMontageTiming::GetPlayRate(1.f, Base);
		TestEqual(TEXT("borné : vitesse max"), Rate, GenMontageTiming::MaxPlayRate, 0.0001f);
		TestTrue(TEXT("borné : avertissement"), GenMontageTiming::ShouldWarn(Rate, GenMontageTiming::GetExpectedFeedRate(Base, Base)));
		TestTrue(TEXT("borné : la 1re frontière tombe après le seuil"), 1.f / Rate > Base + 0.01f);
	}
	// Feed_1 trop court en nourrissage rapide (0.1 s pour 0.15 s, attendu x2) : avertissement
	{
		const float Active = GenFeeding::GetFeedInterval(Base, true);
		const float Rate = GenMontageTiming::GetPlayRate(0.1f, Active);
		TestTrue(TEXT("trop court en rapide : avertissement"), GenMontageTiming::ShouldWarn(Rate, GenMontageTiming::GetExpectedFeedRate(Base, Active)));
	}
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

	// Revue V6-V8, M-4 : curseur sur le lanceur => direction = son avant (comme le client et le serveur)
	FGenAimGeometry OnSelf;
	GenIndicatorRules::ComputeLeapAim(FVector::ZeroVector, FVector::ZeroVector, P, 1, OnSelf, FVector(0.f, -1.f, 0.f));
	TestTrue(TEXT("curseur sur le lanceur : son avant"), OnSelf.Direction.Equals(FVector(0.f, -1.f, 0.f), 0.001f));
	TestTrue(TEXT("repli nul : axe X"), GenIndicatorRules::FlatDirection(FVector::ZeroVector, FVector::UpVector).Equals(FVector(1.f, 0.f, 0.f), 0.001f));
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

	// Vol (vu par tous) : même cercle et mêmes amorces que la visée, au point verrouillé, sans arc de portée
	FGenAimGeometry Aim;
	GenIndicatorRules::ComputeLeapAim(FVector::ZeroVector, FVector(0.f, 500.f, 0.f), P, 2, Aim);
	FGenAimGeometry Flight;
	GenIndicatorRules::ComputeLeapFlight(Aim.TargetCenter, FVector(0.f, 1.f, 0.f), P, 2, Flight);
	TestTrue(TEXT("vol : même centre"), Flight.TargetCenter.Equals(Aim.TargetCenter, 0.01));
	TestEqual(TEXT("vol : même rayon"), Flight.TargetRadius, Aim.TargetRadius, 0.01f);
	TestEqual(TEXT("vol : pas d'arc de portée"), Flight.RangeArcRadius, 0.f);
	TestEqual(TEXT("vol : 2 amorces"), Flight.StubDirections.Num(), 2);
	for (int32 Index = 0; Index < 2 && Index < Flight.StubDirections.Num() && Index < Aim.StubDirections.Num(); ++Index)
	{
		TestTrue(*FString::Printf(TEXT("vol : amorce %d = visée"), Index), Flight.StubDirections[Index].Equals(Aim.StubDirections[Index], 0.001f));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGenHearthFlightsTest, "Gen.Curffe.HearthFlights",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FGenHearthFlightsTest::RunTest(const FString& Parameters)
{
	using namespace CurffeHearthRules;
	// 5 flammes, nourrissage de 3 : les emplacements 4, 3 puis 2 s'éteignent (les flammes restantes gardent 0..1)
	TestEqual(TEXT("1re flamme nourrie : emplacement 4"), GetFedSocketIndex(5, 0, 5), 4);
	TestEqual(TEXT("3e flamme nourrie : emplacement 2"), GetFedSocketIndex(5, 2, 5), 2);
	TestEqual(TEXT("3 flammes, 1re nourrie : emplacement 2"), GetFedSocketIndex(3, 0, 5), 2);
	TestEqual(TEXT("hors bornes"), GetFedSocketIndex(1, 1, 5), INDEX_NONE);

	// Cohérent avec GetSocketStates : l'emplacement quitté est dans le sort
	TArray<ESocket, TInlineAllocator<8>> States;
	GetSocketStates(5, 2, 5, States);
	TestTrue(TEXT("emplacement de la 2e flamme : dans le sort"), States[GetFedSocketIndex(5, 1, 5)] == ESocket::InSpell);
	TestTrue(TEXT("emplacement suivant : encore allumé"), States[GetFedSocketIndex(5, 2, 5)] == ESocket::Lit);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGenBaseExplosionAimTest, "Gen.Visuals.BaseExplosionAim",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FGenBaseExplosionAimTest::RunTest(const FString& Parameters)
{
	// Pyroblast : non nourrissable, éclat de 120 cm sans nourrissage (même règle que le tir : GetShotExplosionRadius)
	GenIndicatorRules::FProjectileAimParams P;
	P.SpawnForwardOffset = 70.f;
	P.Range = 1300.f;
	P.CollisionRadius = 30.f;
	P.BaseExplosionRadius = 120.f;
	FGenAimGeometry G;
	GenIndicatorRules::ComputeProjectileAim(FVector::ZeroVector, FVector::ForwardVector, P, 0, -1.f, G);
	TestEqual(TEXT("éclat de base"), G.CapRadius, GenFeeding::GetShotExplosionRadius(0, P.ExplosionMinFeed, P.ExplosionRadius, P.BaseExplosionRadius), 0.01f);
	TestEqual(TEXT("120 cm"), G.CapRadius, 120.f, 0.01f);
	TestEqual(TEXT("largeur sans échelle"), G.LineWidth, 60.f, 0.01f);
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGenProjectileMarkerDefaultsTest, "Gen.Visuals.ProjectileMarker",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FGenProjectileMarkerDefaultsTest::RunTest(const FString& Parameters)
{
	// V7 : getters lisibles sur le CDO (la visée V1 les lit sans projectile), marqueur = collision × échelle × 1.2
	const AGenProjectile* CDO = GetDefault<AGenProjectile>();
	TestEqual(TEXT("rayon de collision non mis à l'échelle"), CDO->GetCollisionRadius(), 20.f);
	TestEqual(TEXT("portée max"), CDO->GetMaxRange(), 1500.f);
	TestEqual(TEXT("marqueur : collision × 1 × 1.2"), CDO->GetGroundMarkerRadius(), 24.f, 0.001f);
	TestEqual(TEXT("pas d'éclaboussure par défaut"), CDO->GetExplosionRadius(), 0.f);
	TestNull(TEXT("pas de plan sur le CDO (créé en BeginPlay, clients seulement)"), CDO->GetGroundMarker());

	// Le rayon d'éclaboussure est répliqué (taille de l'impact chez les clients)
	const FProperty* Property = FindFProperty<FProperty>(AGenProjectile::StaticClass(), TEXT("ExplosionRadius"));
	TestTrue(TEXT("ExplosionRadius répliqué"), Property && Property->HasAnyPropertyFlags(CPF_Net));

	// Le matériau par défaut existe (MI_Telegraph_Marker) : tout projectile a un marqueur (Art Bible §7.1 règle 6)
	const FObjectProperty* MaterialProperty = FindFProperty<FObjectProperty>(AGenProjectile::StaticClass(), TEXT("GroundMarkerMaterial"));
	TestTrue(TEXT("GroundMarkerMaterial par défaut"), MaterialProperty && MaterialProperty->GetObjectPropertyValue_InContainer(CDO) != nullptr);
	return true;
}

#endif
