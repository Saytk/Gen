#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "InputCoreTypes.h"
#include "UI/GenUIRules.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGenUICooldownFormatTest, "Gen.UI.CooldownFormat",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FGenUICooldownFormatTest::RunTest(const FString& Parameters)
{
	// §4.1 : secondes entières arrondies au-dessus dès 1 s, une décimale sous 1 s, caché si durée totale < 2 s
	TestEqual(TEXT("5.2 s -> 6"), GenUIRules::FormatCooldown(5.2f, 6.f, 2.f), FString(TEXT("6")));
	TestEqual(TEXT("1.0 s -> 1"), GenUIRules::FormatCooldown(1.0f, 6.f, 2.f), FString(TEXT("1")));
	TestEqual(TEXT("1.01 s -> 2"), GenUIRules::FormatCooldown(1.01f, 6.f, 2.f), FString(TEXT("2")));
	TestEqual(TEXT("0.6 s -> 0.6"), GenUIRules::FormatCooldown(0.6f, 6.f, 2.f), FString(TEXT("0.6")));
	TestEqual(TEXT("0.04 s -> 0.1"), GenUIRules::FormatCooldown(0.04f, 6.f, 2.f), FString(TEXT("0.1")));
	TestEqual(TEXT("terminé -> vide"), GenUIRules::FormatCooldown(0.f, 6.f, 2.f), FString());
	TestEqual(TEXT("durée totale 1 s -> caché"), GenUIRules::FormatCooldown(0.8f, 1.f, 2.f), FString());
	TestEqual(TEXT("durée totale exactement 2 s -> affiché"), GenUIRules::FormatCooldown(1.5f, 2.f, 2.f), FString(TEXT("2")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGenUISlotStateTest, "Gen.UI.SlotState",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FGenUISlotStateTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("pas de sort"), GenUIRules::ResolveSlotState(false, true, 3.f), EGenAbilitySlotState::Empty);
	TestEqual(TEXT("étourdi prime sur la recharge"), GenUIRules::ResolveSlotState(true, true, 3.f), EGenAbilitySlotState::Locked);
	TestEqual(TEXT("en recharge"), GenUIRules::ResolveSlotState(true, false, 0.5f), EGenAbilitySlotState::Cooldown);
	TestEqual(TEXT("prêt"), GenUIRules::ResolveSlotState(true, false, 0.f), EGenAbilitySlotState::Ready);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGenUIHexToLinearTest, "Gen.UI.HexToLinear",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FGenUIHexToLinearTest::RunTest(const FString& Parameters)
{
	// Valeurs de référence du guide (§2.2 text.primary, §2.4 energy.charging)
	const FLinearColor TextPrimary = GenUIRules::HexToLinear(TEXT("#F3DEC9"));
	TestEqual(TEXT("text.primary R"), TextPrimary.R, 0.896f, 0.002f);
	TestEqual(TEXT("text.primary G"), TextPrimary.G, 0.730f, 0.002f);
	TestEqual(TEXT("text.primary B"), TextPrimary.B, 0.584f, 0.002f);
	TestEqual(TEXT("alpha par défaut"), TextPrimary.A, 1.f, KINDA_SMALL_NUMBER);

	const FLinearColor Energy = GenUIRules::HexToLinear(TEXT("FFC233"), 0.5f);
	TestEqual(TEXT("sans # accepté, R"), Energy.R, 1.000f, 0.002f);
	TestEqual(TEXT("energy G"), Energy.G, 0.539f, 0.002f);
	TestEqual(TEXT("energy B"), Energy.B, 0.033f, 0.002f);
	TestEqual(TEXT("alpha fourni"), Energy.A, 0.5f, KINDA_SMALL_NUMBER);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGenUIKeyLabelTest, "Gen.UI.KeyLabelFallback",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FGenUIKeyLabelTest::RunTest(const FString& Parameters)
{
	TMap<FKey, FText> Short;
	Short.Add(EKeys::SpaceBar, FText::FromString(TEXT("SPC")));
	TestEqual(TEXT("texte court"), GenUIRules::FallbackKeyLabel(EKeys::SpaceBar, Short).ToString(), FString(TEXT("SPC")));
	TestEqual(TEXT("repli sur le nom de la touche"), GenUIRules::FallbackKeyLabel(EKeys::A, Short).ToString(), EKeys::A.GetDisplayName(false).ToString());
	TestTrue(TEXT("touche invalide -> vide"), GenUIRules::FallbackKeyLabel(FKey(), Short).IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGenUISegmentsTest, "Gen.UI.UltimateSegments",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FGenUISegmentsTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("25/100 -> 1"), GenUIRules::FundedSegments(25.f, 100.f, 4), 1);
	TestEqual(TEXT("49/100 -> 1"), GenUIRules::FundedSegments(49.f, 100.f, 4), 1);
	TestEqual(TEXT("100/100 -> 4"), GenUIRules::FundedSegments(100.f, 100.f, 4), 4);
	TestEqual(TEXT("max nul -> 0"), GenUIRules::FundedSegments(50.f, 0.f, 4), 0);
	TestEqual(TEXT("au-delà borné"), GenUIRules::FundedSegments(150.f, 100.f, 4), 4);
	return true;
}

#endif
