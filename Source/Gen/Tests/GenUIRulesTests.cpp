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
	TestEqual(TEXT("0.95 s -> 1"), GenUIRules::FormatCooldown(0.95f, 6.f, 2.f), FString(TEXT("1")));
	TestEqual(TEXT("0.99 s -> 1"), GenUIRules::FormatCooldown(0.99f, 6.f, 2.f), FString(TEXT("1")));
	TestEqual(TEXT("0.9 s -> 0.9"), GenUIRules::FormatCooldown(0.9f, 6.f, 2.f), FString(TEXT("0.9")));
	TestEqual(TEXT("négatif -> vide"), GenUIRules::FormatCooldown(-0.5f, 6.f, 2.f), FString());
	TestEqual(TEXT("terminé -> vide"), GenUIRules::FormatCooldown(0.f, 6.f, 2.f), FString());
	TestEqual(TEXT("durée totale 1 s -> caché"), GenUIRules::FormatCooldown(0.8f, 1.f, 2.f), FString());
	TestEqual(TEXT("durée totale exactement 2 s -> affiché"), GenUIRules::FormatCooldown(1.5f, 2.f, 2.f), FString(TEXT("2")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGenUICooldownBoxTest, "Gen.UI.CooldownBox",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FGenUICooldownBoxTest::RunTest(const FString& Parameters)
{
	// §2.6 : la boîte suit la classe de format, jamais la valeur
	TestEqual(TEXT("1 chiffre"), GenUIRules::CooldownFormatClass(TEXT("6")), FIntPoint(1, 0));
	TestEqual(TEXT("1 et 4 : même classe"), GenUIRules::CooldownFormatClass(TEXT("1")), GenUIRules::CooldownFormatClass(TEXT("4")));
	TestEqual(TEXT("2 chiffres"), GenUIRules::CooldownFormatClass(TEXT("12")), FIntPoint(2, 0));
	TestEqual(TEXT("0.x"), GenUIRules::CooldownFormatClass(TEXT("0.6")), FIntPoint(2, 1));
	TestEqual(TEXT("vide"), GenUIRules::CooldownFormatClass(FString()), FIntPoint(0, 0));
	TestEqual(TEXT("largeur 2 chiffres"), GenUIRules::CooldownBoxWidth(FIntPoint(2, 0), 17.f, 7.f, 1.f), 36.f);
	TestEqual(TEXT("largeur 0.x"), GenUIRules::CooldownBoxWidth(FIntPoint(2, 1), 17.f, 7.f, 1.f), 43.f);

	// Valeurs de chaque classe, pour mesurer la plus étroite et la plus large
	const TArray<FString> OneDigit = GenUIRules::CooldownClassSamples(FIntPoint(1, 0));
	TestEqual(TEXT("1 chiffre : 1 à 9"), OneDigit.Num(), 9);
	TestTrue(TEXT("1 chiffre : pas de 0"), !OneDigit.Contains(TEXT("0")));
	const TArray<FString> TwoDigits = GenUIRules::CooldownClassSamples(FIntPoint(2, 0));
	TestEqual(TEXT("2 chiffres : 10 à 99"), TwoDigits.Num(), 90);
	TestTrue(TEXT("2 chiffres : 10 et 99"), TwoDigits.Contains(TEXT("10")) && TwoDigits.Contains(TEXT("99")));
	const TArray<FString> Tenths = GenUIRules::CooldownClassSamples(FIntPoint(2, 1));
	TestEqual(TEXT("0.x : 0.1 à 0.9"), Tenths.Num(), 9);
	TestTrue(TEXT("0.x : 0.1 et 0.9"), Tenths.Contains(TEXT("0.1")) && Tenths.Contains(TEXT("0.9")));
	TestEqual(TEXT("3 chiffres : repli sur CooldownBoxWidth"), GenUIRules::CooldownClassSamples(FIntPoint(3, 0)).Num(), 0);

	// Boîte à la largeur du plus large (+ contour), décalée à gauche du quart de l'écart
	const GenUIRules::FCooldownBoxLayout Layout = GenUIRules::CooldownBoxLayout(27.f, 34.f, 1.f);
	TestEqual(TEXT("largeur = plus large + contour"), Layout.Width, 36.f);
	TestEqual(TEXT("décalage = -(34 - 27) / 4"), Layout.CentreShift, -1.75f);
	TestEqual(TEXT("écart nul : centrée"), GenUIRules::CooldownBoxLayout(17.f, 17.f, 0.f).CentreShift, 0.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGenUIRimWidthTest, "Gen.UI.RimWidth",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FGenUIRimWidthTest::RunTest(const FString& Parameters)
{
	// §7.1 : un trait fait au moins 1 px physique
	TestEqual(TEXT("1080p : 1 px"), GenUIRules::RimLayoutWidth(1.f, 1.f), 1.f);
	TestEqual(TEXT("720p plancher 0.9 : 1/0.9 px"), GenUIRules::RimLayoutWidth(1.f, 0.9f), 1.f / 0.9f);
	TestEqual(TEXT("1440p : 1 px de mise en page suffit"), GenUIRules::RimLayoutWidth(1.f, 1.333f), 1.f);
	TestEqual(TEXT("ultime 2 px à 0.9 : 2 px"), GenUIRules::RimLayoutWidth(2.f, 0.9f), 2.f);
	TestEqual(TEXT("échelle 0.4 : 2.5 px"), GenUIRules::RimLayoutWidth(2.f, 0.4f), 2.5f);
	TestEqual(TEXT("échelle inconnue : valeur demandée"), GenUIRules::RimLayoutWidth(1.f, 0.f), 1.f);
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
