#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AbilitySystem/Abilities/GenGA_Projectile.h"
#include "AbilitySystem/GenFeeding.h"
#include "AbilitySystem/GenKnockback.h"
#include "Champions/Curffe/CurffeTuning.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGenFeedingLimitTest, "Gen.Feeding.FeedLimit",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FGenFeedingLimitTest::RunTest(const FString& Parameters)
{
	// Sort à 3 seuils (règle de Curffe), Foyer de 5 flammes
	TestEqual(TEXT("Foyer plein (5), 3 seuils par sort"), GenFeeding::GetFeedLimit(3, 5.f), 3);
	TestEqual(TEXT("2 flammes dispo"), GenFeeding::GetFeedLimit(3, 2.f), 2);
	TestEqual(TEXT("valeur non entière arrondie vers le bas"), GenFeeding::GetFeedLimit(3, 2.5f), 2);
	TestEqual(TEXT("aucune flamme"), GenFeeding::GetFeedLimit(3, 0.f), 0);
	TestEqual(TEXT("ressource négative"), GenFeeding::GetFeedLimit(3, -3.f), 0);
	TestEqual(TEXT("plus de ressource que le max"), GenFeeding::GetFeedLimit(3, 8.f), 3);
	TestEqual(TEXT("sort non nourrissable"), GenFeeding::GetFeedLimit(0, 5.f), 0);
	TestEqual(TEXT("règle générique : un autre plafond reste possible"), GenFeeding::GetFeedLimit(5, 5.f), 5);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGenFeedingCurffeRulesTest, "Gen.Feeding.CurffeRules",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FGenFeedingCurffeRulesTest::RunTest(const FString& Parameters)
{
	// Spec Curffe §2 (décision du 2026-10-08) : 3 seuils max par sort, 0.3 s par flamme, Foyer de 5
	TestEqual(TEXT("3 seuils par sort"), CurffeTuning::MaxFeedPerSpell, 3);
	TestEqual(TEXT("0.3 s par flamme"), CurffeTuning::FeedInterval, 0.3f, KINDA_SMALL_NUMBER);
	TestEqual(TEXT("Combustion : 0.15 s par flamme"), CurffeTuning::FastFeedInterval, 0.15f, KINDA_SMALL_NUMBER);
	TestEqual(TEXT("le Foyer garde 5 flammes"), CurffeTuning::MaxFlames, 5);

	// Foyer plein : un sort à 3 flammes, puis il en reste 2 pour un second sort
	const int32 First = GenFeeding::GetFeedLimit(CurffeTuning::MaxFeedPerSpell, CurffeTuning::MaxFlames);
	TestEqual(TEXT("Foyer plein : 3 flammes nourries, pas 5"), First, 3);
	const int32 Second = GenFeeding::GetFeedLimit(CurffeTuning::MaxFeedPerSpell, CurffeTuning::MaxFlames - First);
	TestEqual(TEXT("puis un sort à 2 flammes"), Second, 2);
	TestEqual(TEXT("puis le Foyer est vide"), GenFeeding::GetFeedLimit(CurffeTuning::MaxFeedPerSpell, CurffeTuning::MaxFlames - First - Second), 0);

	// Un client qui annonce le Foyer entier est ramené à 3, même après un long maintien
	TestEqual(TEXT("serveur : jamais plus de 3"), GenFeeding::ValidateFedCount(5, CurffeTuning::MaxFeedPerSpell, CurffeTuning::MaxFlames, 5.f, CurffeTuning::FeedInterval), 3);

	// Grande boule de feu : zone à 2 flammes, repoussement à 3
	TestFalse(TEXT("1 flamme : pas de zone"), GenFeeding::ReachesThreshold(1, CurffeTuning::GreatFireballSplashMinFeed));
	TestTrue(TEXT("2 flammes : zone"), GenFeeding::ReachesThreshold(2, CurffeTuning::GreatFireballSplashMinFeed));
	TestFalse(TEXT("2 flammes : pas de repoussement"), GenFeeding::ReachesThreshold(2, CurffeTuning::GreatFireballKnockbackMinFeed));
	TestTrue(TEXT("3 flammes : repoussement"), GenFeeding::ReachesThreshold(3, CurffeTuning::GreatFireballKnockbackMinFeed));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGenFeedingValidateTest, "Gen.Feeding.ServerValidation",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FGenFeedingValidateTest::RunTest(const FString& Parameters)
{
	// ValidateFedCount(ClientFed, MaxFeed, Available, ElapsedFeedTime, FeedInterval) : 3 seuils, 0.3 s
	TestEqual(TEXT("annonce honnête"), GenFeeding::ValidateFedCount(3, 3, 5.f, 1.0f, 0.3f), 3);
	TestEqual(TEXT("borné par la ressource serveur"), GenFeeding::ValidateFedCount(3, 3, 2.f, 1.0f, 0.3f), 2);
	TestEqual(TEXT("borné par le temps (1 intervalle + 1 de tolérance)"), GenFeeding::ValidateFedCount(3, 3, 5.f, 0.35f, 0.3f), 2);
	TestEqual(TEXT("gigue : 0.88 s pour 3 flammes accepté"), GenFeeding::ValidateFedCount(3, 3, 5.f, 0.88f, 0.3f), 3);
	TestEqual(TEXT("annonce négative"), GenFeeding::ValidateFedCount(-1, 3, 5.f, 1.0f, 0.3f), 0);
	TestEqual(TEXT("annonce au-delà du max"), GenFeeding::ValidateFedCount(9, 3, 9.f, 5.0f, 0.3f), 3);
	TestEqual(TEXT("tap immédiat"), GenFeeding::ValidateFedCount(0, 3, 5.f, 0.0f, 0.3f), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGenFeedingReportTest, "Gen.Feeding.ReportedCount",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FGenFeedingReportTest::RunTest(const FString& Parameters)
{
	// ReconcileDisplayedFed(Displayed, ServerEstimate, Reported, MaxFeed, Available, ElapsedFeedTime, FeedInterval) : 3 seuils, 0.3 s
	constexpr int32 NoReport = INDEX_NONE;

	// Sans annonce : l'estimation du serveur, bornée à la ressource et au maximum
	TestEqual(TEXT("estimation seule"), GenFeeding::ReconcileDisplayedFed(1, 2, NoReport, 3, 5.f, 0.7f, 0.3f), 2);
	TestEqual(TEXT("estimation bornée par la ressource"), GenFeeding::ReconcileDisplayedFed(0, 3, NoReport, 3, 2.f, 1.f, 0.3f), 2);

	// Annonce : elle remplace l'estimation, bornée par le temps comme le tir (floor(écoulé / I) + 1)
	TestEqual(TEXT("estimation en retard de 2 ticks, corrigée vers le haut"), GenFeeding::ReconcileDisplayedFed(1, 1, 3, 3, 5.f, 0.95f, 0.3f), 3);
	TestEqual(TEXT("bluff de 3 flammes dès l'activation"), GenFeeding::ReconcileDisplayedFed(0, 0, 3, 3, 5.f, 0.f, 0.3f), 1);
	TestEqual(TEXT("bluff : la borne monte avec le temps"), GenFeeding::ReconcileDisplayedFed(1, 0, 3, 3, 5.f, 0.35f, 0.3f), 2);
	TestEqual(TEXT("annonce bornée par la ressource"), GenFeeding::ReconcileDisplayedFed(0, 0, 3, 3, 2.f, 1.f, 0.3f), 2);
	TestEqual(TEXT("annonce bornée par le maximum du sort, Foyer plein"), GenFeeding::ReconcileDisplayedFed(0, 3, 5, 3, 5.f, 2.f, 0.3f), 3);

	// Jamais de recul pendant le sort (4 -> 3 vu par les autres joueurs, revue M-1)
	TestEqual(TEXT("annonce plus basse que l'affichage : l'affichage reste"), GenFeeding::ReconcileDisplayedFed(3, 3, 2, 3, 5.f, 0.9f, 0.3f), 3);
	TestEqual(TEXT("annonce à 0 après un tick du serveur : reste à 1"), GenFeeding::ReconcileDisplayedFed(1, 1, 0, 3, 5.f, 0.31f, 0.3f), 1);
	TestEqual(TEXT("estimation qui recule : l'affichage reste"), GenFeeding::ReconcileDisplayedFed(2, 1, NoReport, 3, 5.f, 0.6f, 0.3f), 2);
	TestEqual(TEXT("affichage négatif ignoré"), GenFeeding::ReconcileDisplayedFed(-1, 0, NoReport, 3, 5.f, 0.f, 0.3f), 0);

	// Suite d'appels d'un même sort : affichage monotone
	int32 Shown = 0;
	const int32 Estimates[] = { 1, 2, 2, 2 };
	const int32 Reports[] = { NoReport, NoReport, 1, 1 };
	const float Times[] = { 0.4f, 0.7f, 0.72f, 0.8f };
	for (int32 Step = 0; Step < 4; ++Step)
	{
		const int32 Next = GenFeeding::ReconcileDisplayedFed(Shown, Estimates[Step], Reports[Step], 3, 5.f, Times[Step], 0.3f);
		TestTrue(*FString::Printf(TEXT("étape %d : %d -> %d ne recule pas"), Step, Shown, Next), Next >= Shown);
		Shown = Next;
	}
	TestEqual(TEXT("suite : 2 affichées"), Shown, 2);

	TestTrue(TEXT("retard de l'estimation positif et moins d'un demi-intervalle"), GenFeeding::ServerEstimateLag > 0.f && GenFeeding::ServerEstimateLag < CurffeTuning::FeedInterval * 0.5f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGenCastTimingTest, "Gen.Feeding.ServerCastWait",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FGenCastTimingTest::RunTest(const FString& Parameters)
{
	// GetServerCastWait(CastTime, ElapsedCastTime, Tolerance)
	TestEqual(TEXT("visée à l'heure"), GenFeeding::GetServerCastWait(0.5f, 0.5f, 0.1f), 0.f, KINDA_SMALL_NUMBER);
	TestEqual(TEXT("visée en retard"), GenFeeding::GetServerCastWait(0.5f, 0.7f, 0.1f), 0.f, KINDA_SMALL_NUMBER);
	TestEqual(TEXT("gigue absorbée (0.45 s mesurées)"), GenFeeding::GetServerCastWait(0.5f, 0.45f, 0.1f), 0.f, KINDA_SMALL_NUMBER);
	TestEqual(TEXT("limite de la tolérance"), GenFeeding::GetServerCastWait(0.5f, 0.4f, 0.1f), 0.f, KINDA_SMALL_NUMBER);
	TestEqual(TEXT("visée trop tôt : attente du reste"), GenFeeding::GetServerCastWait(0.5f, 0.2f, 0.1f), 0.2f, KINDA_SMALL_NUMBER);
	TestEqual(TEXT("visée immédiate"), GenFeeding::GetServerCastWait(0.5f, 0.f, 0.1f), 0.4f, KINDA_SMALL_NUMBER);
	TestEqual(TEXT("sort instantané"), GenFeeding::GetServerCastWait(0.f, 0.f, 0.1f), 0.f, KINDA_SMALL_NUMBER);
	TestEqual(TEXT("tolérance négative ignorée"), GenFeeding::GetServerCastWait(0.5f, 0.45f, -1.f), 0.05f, KINDA_SMALL_NUMBER);

	const float Tolerance = GenFeeding::CastTimeTolerance;
	// Boule de feu (0.35 s) sous 60 ms ± 15 ms : la gigue (± 30 ms + une image) ne diffère jamais le tir
	TestEqual(TEXT("boule de feu, gigue défavorable"), GenFeeding::GetServerCastWait(0.35f, 0.35f - 0.03f - 0.017f, Tolerance), 0.f, KINDA_SMALL_NUMBER);
	// Activation perdue puis renvoyée (environ un aller-retour de retard) : le projectile attend le reste
	TestEqual(TEXT("boule de feu, activation renvoyée"), GenFeeding::GetServerCastWait(0.35f, 0.21f, Tolerance), 0.04f, KINDA_SMALL_NUMBER);
	// Client tricheur qui colle la visée à l'activation : l'incantation est imposée à la tolérance près
	TestEqual(TEXT("visée collée à l'activation"), GenFeeding::GetServerCastWait(0.5f, 0.f, Tolerance), 0.5f - Tolerance, KINDA_SMALL_NUMBER);

	// Signalement d'une visée en avance (Warning) : au-delà de tolérance + 0.25 s avant la fin
	TestFalse(TEXT("activation renvoyée : pas de signalement"), GenFeeding::IsAimSuspiciouslyEarly(0.5f, 0.3f, Tolerance));
	TestFalse(TEXT("limite du signalement"), GenFeeding::IsAimSuspiciouslyEarly(0.5f, 0.5f - Tolerance - GenFeeding::EarlyAimWarningMargin, Tolerance));
	TestTrue(TEXT("visée collée à l'activation : signalée"), GenFeeding::IsAimSuspiciouslyEarly(0.5f, 0.f, Tolerance));
	TestFalse(TEXT("incantation courte (0.3 s) : jamais signalée"), GenFeeding::IsAimSuspiciouslyEarly(0.3f, 0.f, Tolerance));

	TestTrue(TEXT("tolérance petite devant les incantations"), GenFeeding::CastTimeTolerance > 0.f && GenFeeding::CastTimeTolerance <= 0.1f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGenFeedingNextTickTest, "Gen.Feeding.NextTickDelay",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FGenFeedingNextTickTest::RunTest(const FString& Parameters)
{
	// GetNextFeedTickDelay(FeedStartTime, FedCount, FeedInterval, Now) : flamme k+1 à début + (k+1) x intervalle
	TestEqual(TEXT("première flamme à l'appui"), GenFeeding::GetNextFeedTickDelay(10.f, 0, 0.3f, 10.f), 0.3f, KINDA_SMALL_NUMBER);
	TestEqual(TEXT("première flamme, une image plus tard"), GenFeeding::GetNextFeedTickDelay(10.f, 0, 0.3f, 10.0167f), 0.2833f, 1.e-4f);
	TestEqual(TEXT("tick en retard : le suivant n'hérite pas du retard"), GenFeeding::GetNextFeedTickDelay(10.f, 1, 0.3f, 10.31f), 0.29f, 1.e-4f);
	TestEqual(TEXT("2e tick en retard de 15 ms : 3e toujours à 10.9"), GenFeeding::GetNextFeedTickDelay(10.f, 2, 0.3f, 10.615f), 0.285f, 1.e-4f);
	TestEqual(TEXT("saccade : seuil déjà dépassé => tick suivant immédiat"), GenFeeding::GetNextFeedTickDelay(10.f, 2, 0.3f, 10.95f), 0.f, KINDA_SMALL_NUMBER);
	// Combustion : nourrissage à 0.15 s, les ticks en retard ne dérivent pas
	TestEqual(TEXT("intervalle court (Combustion) : 3e tick"), GenFeeding::GetNextFeedTickDelay(10.f, 2, CurffeTuning::FastFeedInterval, 10.308f), 0.142f, 1.e-4f);
	TestEqual(TEXT("intervalle nul"), GenFeeding::GetNextFeedTickDelay(10.f, 2, 0.f, 10.5f), 0.f, KINDA_SMALL_NUMBER);
	// Serveur pour un client distant : estimation décalée de ServerEstimateLag (début du nourrissage inchangé)
	TestEqual(TEXT("estimation du serveur : 1re flamme à 0.3 + 0.1 s"), GenFeeding::GetNextFeedTickDelay(10.f + GenFeeding::ServerEstimateLag, 0, 0.3f, 10.f), 0.4f, 1.e-4f);
	TestEqual(TEXT("estimation du serveur : 3e flamme à 0.9 + 0.1 s"), GenFeeding::GetNextFeedTickDelay(10.f + GenFeeding::ServerEstimateLag, 2, 0.3f, 10.62f), 0.38f, 1.e-4f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGenFeedingScaleTest, "Gen.Feeding.Scaling",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FGenFeedingScaleTest::RunTest(const FString& Parameters)
{
	// Grande boule de feu : 25 m/s sans flamme, 16 m/s à 3 flammes
	TestEqual(TEXT("0 flamme"), GenFeeding::ScaleByFeed(2500.f, 1600.f, 0, 3), 2500.f);
	TestEqual(TEXT("1 flamme"), GenFeeding::ScaleByFeed(2500.f, 1600.f, 1, 3), 2200.f, 1.e-2f);
	TestEqual(TEXT("2 flammes"), GenFeeding::ScaleByFeed(2500.f, 1600.f, 2, 3), 1900.f, 1.e-2f);
	TestEqual(TEXT("3 flammes"), GenFeeding::ScaleByFeed(2500.f, 1600.f, 3, 3), 1600.f);
	TestEqual(TEXT("au-delà du max"), GenFeeding::ScaleByFeed(2500.f, 1600.f, 5, 3), 1600.f);
	TestEqual(TEXT("MaxFeed nul"), GenFeeding::ScaleByFeed(1.f, 2.f, 3, 0), 1.f);
	TestTrue(TEXT("seuil atteint"), GenFeeding::ReachesThreshold(3, 3));
	TestFalse(TEXT("seuil non atteint"), GenFeeding::ReachesThreshold(1, 2));
	TestFalse(TEXT("seuil désactivé"), GenFeeding::ReachesThreshold(3, 0));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGenKnockbackVelocityTest, "Gen.Knockback.LaunchVelocity",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FGenKnockbackVelocityTest::RunTest(const FString& Parameters)
{
	const float GravityZ = -980.f;
	const FVector Velocity = GenKnockback::ComputeLaunchVelocity(FVector(1.f, 0.f, 0.f), 400.f, GravityZ);
	const float AirTime = 2.f * GenKnockback::UpSpeed / 980.f;
	TestEqual(TEXT("distance parcourue en vol = 400 cm"), static_cast<float>(Velocity.X * AirTime), 400.f, 1.f);
	TestEqual(TEXT("pas de composante latérale"), static_cast<float>(Velocity.Y), 0.f, KINDA_SMALL_NUMBER);
	TestEqual(TEXT("petit saut vertical"), static_cast<float>(Velocity.Z), GenKnockback::UpSpeed, KINDA_SMALL_NUMBER);

	const FVector Diagonal = GenKnockback::ComputeLaunchVelocity(FVector(1.f, 1.f, 5.f), 400.f, GravityZ);
	TestEqual(TEXT("direction aplatie et normalisée"), static_cast<float>(FVector(Diagonal.X, Diagonal.Y, 0.f).Size() * AirTime), 400.f, 1.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGenCastDefaultsTest, "Gen.Feeding.CastDefaults",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FGenCastDefaultsTest::RunTest(const FString& Parameters)
{
	// Revue de la Task 5 (I-4) : les valeurs par défaut des sorts à incantation viennent de CurffeTuning (jamais de littéraux).
	// Lues par réflexion (propriétés protégées de UGenGA_Cast) sur le CDO d'une classe concrète.
	const UGenGA_Projectile* CDO = GetDefault<UGenGA_Projectile>();
	const FIntProperty* MaxFeedProperty = FindFProperty<FIntProperty>(UGenGA_Cast::StaticClass(), TEXT("MaxFeed"));
	const FFloatProperty* FeedIntervalProperty = FindFProperty<FFloatProperty>(UGenGA_Cast::StaticClass(), TEXT("FeedInterval"));
	if (!TestNotNull(TEXT("propriété MaxFeed"), MaxFeedProperty) || !TestNotNull(TEXT("propriété FeedInterval"), FeedIntervalProperty))
	{
		return false;
	}
	TestEqual(TEXT("MaxFeed = CurffeTuning::MaxFeedPerSpell"), MaxFeedProperty->GetPropertyValue_InContainer(CDO), CurffeTuning::MaxFeedPerSpell);
	TestEqual(TEXT("FeedInterval = CurffeTuning::FeedInterval"), FeedIntervalProperty->GetPropertyValue_InContainer(CDO), CurffeTuning::FeedInterval, KINDA_SMALL_NUMBER);
	return true;
}

#endif
