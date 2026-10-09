#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Game/GenDevTuning.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGenDevTuningRulesTest, "Gen.Dev.TuningRules",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FGenDevTuningRulesTest::RunTest(const FString& Parameters)
{
	// Valeurs par défaut = jeu normal : la recharge ne change pas
	const FGenDevTuning Defaults;
	TestEqual(TEXT("défaut : recharge intacte"), GenDevTuning::ScaleCooldown(Defaults, 6.f), 6.f);

	FGenDevTuning Tuning;
	Tuning.CooldownScale = 0.5f;
	TestEqual(TEXT("recharges x0.5"), GenDevTuning::ScaleCooldown(Tuning, 6.f), 3.f);
	Tuning.bNoCooldowns = true;
	TestEqual(TEXT("aucune recharge"), GenDevTuning::ScaleCooldown(Tuning, 6.f), 0.f);

	// Le serveur borne ce qu'un client envoie
	FGenDevTuning Wild;
	Wild.CooldownScale = -4.f;
	Wild.CastTimeScale = 99.f;
	Wild.GameSpeed = 0.f;
	const FGenDevTuning Clamped = Wild.Clamped();
	TestEqual(TEXT("recharges bornées"), Clamped.CooldownScale, GenDevTuning::MinCooldownScale);
	TestEqual(TEXT("incantation bornée"), Clamped.CastTimeScale, GenDevTuning::MaxCastTimeScale);
	TestEqual(TEXT("vitesse bornée"), Clamped.GameSpeed, GenDevTuning::MinGameSpeed);
	TestTrue(TEXT("défauts inchangés par les bornes"), Defaults.Clamped() == Defaults);
	return true;
}

#endif
