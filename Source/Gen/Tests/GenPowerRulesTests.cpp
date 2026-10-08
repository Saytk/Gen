#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AbilitySystem/GenEnergy.h"
#include "AbilitySystem/GenFeeding.h"
#include "AbilitySystem/GenHitRules.h"
#include "AbilitySystem/GenResilience.h"
#include "Champions/Curffe/CurffeTuning.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGenFastFeedingTest, "Gen.Feeding.FastInterval",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FGenFastFeedingTest::RunTest(const FString& Parameters)
{
	// GetFeedInterval(BaseInterval, bFastFeeding), avec les valeurs de Curffe (3 seuils, 0.3 s par flamme)
	TestEqual(TEXT("intervalle normal"), GenFeeding::GetFeedInterval(CurffeTuning::FeedInterval, false), CurffeTuning::FeedInterval, KINDA_SMALL_NUMBER);
	TestEqual(TEXT("Combustion : 0.15 s par flamme (CurffeTuning::FastFeedInterval)"), GenFeeding::GetFeedInterval(CurffeTuning::FeedInterval, true), CurffeTuning::FastFeedInterval, KINDA_SMALL_NUMBER);
	TestTrue(TEXT("jamais nul"), GenFeeding::GetFeedInterval(0.f, true) > 0.f);

	// Avec l'intervalle rapide, les ticks calés sur le début du nourrissage (GetNextFeedTickDelay,
	// correctif de la dérive du plan 1) tombent à 0.15, 0.30, 0.45 : le 3e seuil (le dernier) à 0.45 s au lieu de 0.9 s
	const float Fast = GenFeeding::GetFeedInterval(CurffeTuning::FeedInterval, true);
	TestEqual(TEXT("3e flamme à 0.45 s"), GenFeeding::GetNextFeedTickDelay(0.f, 2, Fast, 0.3f), 0.15f, 0.0001f);
	TestEqual(TEXT("tick en retard d'une image : pas de dérive"), GenFeeding::GetNextFeedTickDelay(0.f, 1, Fast, 0.167f), 0.133f, 0.0001f);

	// Validation serveur avec l'intervalle rapide : 3 flammes en 0.47 s acceptées
	TestEqual(TEXT("3 en 0.47 s validées"), GenFeeding::ValidateFedCount(3, CurffeTuning::MaxFeedPerSpell, 5.f, 0.47f, Fast), 3);
	TestEqual(TEXT("avec l'intervalle normal, ce serait 2"), GenFeeding::ValidateFedCount(3, CurffeTuning::MaxFeedPerSpell, 5.f, 0.47f, CurffeTuning::FeedInterval), 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGenCanAffordTest, "Gen.Energy.CanAfford",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FGenCanAffordTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("25 pour Flamme vivante"), GenEnergy::CanAfford(25.f, 25.f));
	TestFalse(TEXT("24.99 ne suffit pas"), GenEnergy::CanAfford(24.99f, 25.f));
	TestTrue(TEXT("100 pour Combustion"), GenEnergy::CanAfford(100.f, 100.f));
	TestTrue(TEXT("dérive flottante tolérée"), GenEnergy::CanAfford(99.99999f, 100.f));
	TestFalse(TEXT("99 ne suffit pas"), GenEnergy::CanAfford(99.f, 100.f));
	TestTrue(TEXT("sort gratuit"), GenEnergy::CanAfford(0.f, 0.f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGenResilienceHistoryTest, "Gen.Combat.ResilienceHistory",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FGenResilienceHistoryTest::RunTest(const FString& Parameters)
{
	{
		// 3 étourdissements de 1 s à 0, 1.2 et 2.4 : 3 s cumulées => immunité jusqu'à 1.5 s après la fin du 3e
		GenResilience::FHardCCHistory History;
		TestEqual(TEXT("1er"), History.Record(0.f, 1.f), 0.f);
		TestEqual(TEXT("2e"), History.Record(1.2f, 1.f), 0.f);
		TestEqual(TEXT("3e : immunité (1 s restante + 1.5 s)"), History.Record(2.4f, 1.f), 2.5f, 0.001f);
		TestEqual(TEXT("historique remis à zéro après l'immunité"), History.Record(10.f, 1.f), 0.f);
	}
	{
		// Deux étourdissements qui se chevauchent comptent une fois (union des intervalles)
		GenResilience::FHardCCHistory History;
		History.Record(0.f, 1.f);
		TestEqual(TEXT("chevauchement : 1.5 s seulement"), History.Record(0.5f, 1.f), 0.f);
		TestEqual(TEXT("puis 1 s de plus : 2.5 s atteintes"), History.Record(2.f, 1.f), 2.5f, 0.001f);
	}
	{
		// Fenêtre glissante de 5 s
		GenResilience::FHardCCHistory History;
		History.Record(0.f, 1.f);
		TestEqual(TEXT("le premier est sorti de la fenêtre"), History.Record(6.f, 1.f), 0.f);
		TestEqual(TEXT("2 s dans la fenêtre"), History.Record(7.f, 1.f), 0.f);
	}
	{
		// Un long contrôle seul (ultime) suffit
		GenResilience::FHardCCHistory History;
		TestEqual(TEXT("2.5 s d'un coup"), History.Record(0.f, 2.5f), 4.f, 0.001f);
	}
	{
		// Contrôle à cheval sur le début de la fenêtre : seule la partie dans la fenêtre compte
		GenResilience::FHardCCHistory History;
		History.Record(0.f, 2.f);
		TestEqual(TEXT("1.5 s (0.5 à 2) + 1 s"), History.Record(5.5f, 1.f), 2.5f, 0.001f);
	}
	{
		GenResilience::FHardCCHistory History;
		History.Record(0.f, 2.f);
		History.Reset();
		TestEqual(TEXT("Reset (mort) oublie tout"), History.Record(0.5f, 1.f), 0.f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGenUntouchableRuleTest, "Gen.Combat.UntouchableRule",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FGenUntouchableRuleTest::RunTest(const FString& Parameters)
{
	// Resolve(bCountering, bUntouchable, Kind)
	TestTrue(TEXT("projectile sur un intouchable : ignoré"), GenHitRules::Resolve(false, true, EGenHitKind::Projectile) == EGenHitResponse::Ignored);
	TestTrue(TEXT("zone sur un intouchable : ignorée"), GenHitRules::Resolve(false, true, EGenHitKind::Area) == EGenHitResponse::Ignored);
	TestTrue(TEXT("intouchable prime sur le contre (rien n'est bloqué, pas de récompense)"), GenHitRules::Resolve(true, true, EGenHitKind::Projectile) == EGenHitResponse::Ignored);
	TestTrue(TEXT("sans intouchable : règle du contre"), GenHitRules::Resolve(true, false, EGenHitKind::Melee) == EGenHitResponse::Countered);
	return true;
}

#endif
