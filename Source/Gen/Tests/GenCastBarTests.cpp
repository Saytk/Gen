#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "UI/GenCastBarRules.h"

namespace GenCastBarTests
{
	constexpr float Tolerance = 1.e-4f;

	/** Grande boule de feu type : 5 flammes, une toutes les 0.2 s, 0.5 s d'incantation, appui à 10 s. */
	GenCastBar::FParams MakeFeed(int32 Slots = 5, float Interval = 0.2f, float CastTime = 0.5f)
	{
		GenCastBar::FParams Params;
		Params.FeedSlots = Slots;
		Params.FeedInterval = Interval;
		Params.CastTime = CastTime;
		Params.StartTime = 10.f;
		return Params;
	}

	GenCastBar::FParams MakeEnded(float EndTime, int32 Fed, int32 Slots = 5, float Interval = 0.2f, float CastTime = 0.5f)
	{
		GenCastBar::FParams Params = MakeFeed(Slots, Interval, CastTime);
		Params.FeedEndTime = EndTime;
		Params.FedCount = Fed;
		return Params;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGenCastBarFeedingTest, "Gen.CastBar.Feeding",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FGenCastBarFeedingTest::RunTest(const FString& Parameters)
{
	using namespace GenCastBarTests;
	const GenCastBar::FParams Params = MakeFeed();

	const GenCastBar::FLayout AtPress = GenCastBar::ComputeLayout(Params, 10.f);
	TestTrue(TEXT("sort nourri"), AtPress.bFed);
	TestEqual(TEXT("longueur = 5 x 0.2 + 0.5"), AtPress.TotalDuration, 1.5f, Tolerance);
	TestEqual(TEXT("vide à l'appui"), AtPress.Fill, 0.f, Tolerance);
	TestEqual(TEXT("compteur à 0"), AtPress.Counter, 0);
	if (TestEqual(TEXT("un cran par flamme disponible"), AtPress.Ticks.Num(), 5))
	{
		for (int32 K = 1; K <= 5; ++K)
		{
			TestEqual(*FString::Printf(TEXT("cran %d à k x 0.2 / 1.5"), K), AtPress.Ticks[K - 1], K * 0.2f / 1.5f, Tolerance);
		}
	}

	const GenCastBar::FLayout Mid = GenCastBar::ComputeLayout(Params, 10.3f);
	TestEqual(TEXT("remplissage en temps réel"), Mid.Fill, 0.3f / 1.5f, Tolerance);
	TestEqual(TEXT("1 flamme passée"), Mid.Counter, 1);
	TestEqual(TEXT("les crans ne bougent pas pendant le nourrissage"), Mid.Ticks.Num(), 5);

	TestEqual(TEXT("seuil exact : 2 flammes"), GenCastBar::ComputeLayout(Params, 10.4f).Counter, 2);
	TestEqual(TEXT("juste avant le seuil : 1 flamme"), GenCastBar::ComputeLayout(Params, 10.39f).Counter, 1);

	// Fin du nourrissage pas encore connue (signal du client en route) : le compteur plafonne, la barre continue
	const GenCastBar::FLayout Late = GenCastBar::ComputeLayout(Params, 11.2f);
	TestEqual(TEXT("compteur plafonné aux flammes disponibles"), Late.Counter, 5);
	TestEqual(TEXT("la barre continue dans l'incantation"), Late.Fill, 1.2f / 1.5f, Tolerance);
	TestEqual(TEXT("remplissage borné à 1"), GenCastBar::ComputeLayout(Params, 12.f).Fill, 1.f, Tolerance);
	TestEqual(TEXT("horloge en retard sur le début : vide"), GenCastBar::ComputeLayout(Params, 9.9f).Fill, 0.f, Tolerance);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGenCastBarReleaseTest, "Gen.CastBar.Release",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FGenCastBarReleaseTest::RunTest(const FString& Parameters)
{
	using namespace GenCastBarTests;

	// Relâché à 10.5 s avec 2 flammes : total visé 2 x 0.2 + 0.5 = 0.9 s
	const GenCastBar::FParams Params = MakeEnded(10.5f, 2);

	const GenCastBar::FLayout AtRelease = GenCastBar::ComputeLayout(Params, 10.5f);
	TestEqual(TEXT("au relâché : longueur de nourrissage"), AtRelease.TotalDuration, 1.5f, Tolerance);
	TestEqual(TEXT("le temps après le dernier seuil est abandonné"), AtRelease.Fill, 0.4f / 1.5f, Tolerance);
	TestEqual(TEXT("compteur = flammes nourries"), AtRelease.Counter, 2);
	if (TestEqual(TEXT("crans 1..N seulement"), AtRelease.Ticks.Num(), 2))
	{
		TestEqual(TEXT("cran 1"), AtRelease.Ticks[0], 0.2f / 1.5f, Tolerance);
		TestEqual(TEXT("cran 2"), AtRelease.Ticks[1], 0.4f / 1.5f, Tolerance);
	}

	const GenCastBar::FLayout Collapsing = GenCastBar::ComputeLayout(Params, 10.55f);
	TestEqual(TEXT("repli à mi-course"), Collapsing.TotalDuration, 1.2f, Tolerance);
	TestEqual(TEXT("remplissage pendant le repli"), Collapsing.Fill, 0.45f / 1.2f, Tolerance);
	if (TestEqual(TEXT("2 crans pendant le repli"), Collapsing.Ticks.Num(), 2))
	{
		TestEqual(TEXT("cran 2 suit le repli"), Collapsing.Ticks[1], 0.4f / 1.2f, Tolerance);
	}

	const GenCastBar::FLayout Collapsed = GenCastBar::ComputeLayout(Params, 10.6f);
	TestEqual(TEXT("repli terminé en 100 ms"), Collapsed.TotalDuration, 0.9f, Tolerance);
	TestEqual(TEXT("la même barre continue"), Collapsed.Fill, 0.5f / 0.9f, Tolerance);
	if (TestEqual(TEXT("2 crans après le repli"), Collapsed.Ticks.Num(), 2))
	{
		TestEqual(TEXT("crans recalés"), Collapsed.Ticks[0], 0.2f / 0.9f, Tolerance);
	}

	TestEqual(TEXT("pleine à la fin de l'incantation"), GenCastBar::ComputeLayout(Params, 11.f).Fill, 1.f, Tolerance);
	TestEqual(TEXT("bornée après"), GenCastBar::ComputeLayout(Params, 11.5f).Fill, 1.f, Tolerance);
	TestEqual(TEXT("compteur figé"), GenCastBar::ComputeLayout(Params, 11.5f).Counter, 2);

	// Horloge locale un peu avant la fin répliquée : rien ne recule ni n'avance
	const GenCastBar::FLayout Early = GenCastBar::ComputeLayout(Params, 10.45f);
	TestEqual(TEXT("avant la fin : pas de repli"), Early.TotalDuration, 1.5f, Tolerance);
	TestEqual(TEXT("avant la fin : seuil N"), Early.Fill, 0.4f / 1.5f, Tolerance);

	TestEqual(TEXT("repli de 100 ms"), GenCastBar::CollapseDuration, 0.1f, Tolerance);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGenCastBarEdgeTest, "Gen.CastBar.EdgeCases",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FGenCastBarEdgeTest::RunTest(const FString& Parameters)
{
	using namespace GenCastBarTests;

	// FeedSlots = 0 : incantation normale depuis le début, fin de nourrissage ignorée
	{
		const GenCastBar::FLayout Normal = GenCastBar::ComputeLayout(MakeFeed(0), 10.25f);
		TestFalse(TEXT("0 emplacement : barre normale"), Normal.bFed);
		TestEqual(TEXT("0 emplacement : durée = incantation"), Normal.TotalDuration, 0.5f, Tolerance);
		TestEqual(TEXT("0 emplacement : remplissage"), Normal.Fill, 0.5f, Tolerance);
		TestEqual(TEXT("0 emplacement : aucun cran"), Normal.Ticks.Num(), 0);
		TestEqual(TEXT("0 emplacement : compteur"), Normal.Counter, 0);
		TestEqual(TEXT("0 emplacement : fin de nourrissage ignorée"), GenCastBar::ComputeLayout(MakeEnded(10.05f, 0, 0), 10.25f).Fill, 0.5f, Tolerance);
		TestFalse(TEXT("intervalle nul : barre normale"), GenCastBar::ComputeLayout(MakeFeed(5, 0.f), 10.25f).bFed);
	}

	// N = 0 : relâché avant la première flamme, tout se replie
	{
		const GenCastBar::FParams Params = MakeEnded(10.1f, 0);
		const GenCastBar::FLayout Layout = GenCastBar::ComputeLayout(Params, 10.2f);
		TestEqual(TEXT("N = 0 : il ne reste que l'incantation"), Layout.TotalDuration, 0.5f, Tolerance);
		TestEqual(TEXT("N = 0 : remplissage"), Layout.Fill, 0.1f / 0.5f, Tolerance);
		TestEqual(TEXT("N = 0 : aucun cran"), Layout.Ticks.Num(), 0);
		TestEqual(TEXT("N = 0 : compteur"), Layout.Counter, 0);
		TestTrue(TEXT("N = 0 : toujours un sort nourri"), Layout.bFed);
	}

	// N = FeedSlots : rien à replier
	{
		const GenCastBar::FParams Params = MakeEnded(11.f, 5);
		const GenCastBar::FLayout Layout = GenCastBar::ComputeLayout(Params, 11.05f);
		TestEqual(TEXT("N max : longueur inchangée"), Layout.TotalDuration, 1.5f, Tolerance);
		TestEqual(TEXT("N max : remplissage continu"), Layout.Fill, 1.05f / 1.5f, Tolerance);
		TestEqual(TEXT("N max : tous les crans"), Layout.Ticks.Num(), 5);
		TestEqual(TEXT("N max : compteur"), Layout.Counter, 5);
		TestEqual(TEXT("compte annoncé au-delà des emplacements"), GenCastBar::ComputeLayout(MakeEnded(11.f, 7), 11.05f).Counter, 5);
	}

	// Relâché pile sur un seuil : pas de saut du remplissage
	{
		const float Before = GenCastBar::ComputeLayout(MakeFeed(), 10.4f).Fill;
		const float After = GenCastBar::ComputeLayout(MakeEnded(10.4f, 2), 10.4f).Fill;
		TestEqual(TEXT("relâché sur un seuil : continuité"), After, Before, Tolerance);
	}

	// CastTime = 0 : la barre se termine sur le dernier seuil
	{
		const GenCastBar::FLayout Feeding = GenCastBar::ComputeLayout(MakeFeed(3, 0.2f, 0.f), 10.1f);
		TestEqual(TEXT("sans incantation : longueur"), Feeding.TotalDuration, 0.6f, Tolerance);
		TestEqual(TEXT("sans incantation : remplissage"), Feeding.Fill, 0.1f / 0.6f, Tolerance);
		TestEqual(TEXT("sans incantation : 3 crans"), Feeding.Ticks.Num(), 3);

		const GenCastBar::FParams Ended = MakeEnded(10.3f, 1, 3, 0.2f, 0.f);
		TestEqual(TEXT("sans incantation : repli"), GenCastBar::ComputeLayout(Ended, 10.35f).TotalDuration, 0.4f, Tolerance);
		TestEqual(TEXT("sans incantation : remplissage pendant le repli"), GenCastBar::ComputeLayout(Ended, 10.35f).Fill, 0.5f, Tolerance);
		TestEqual(TEXT("sans incantation : pleine après le repli"), GenCastBar::ComputeLayout(Ended, 10.4f).Fill, 1.f, Tolerance);

		const GenCastBar::FLayout Empty = GenCastBar::ComputeLayout(MakeEnded(10.1f, 0, 3, 0.2f, 0.f), 10.3f);
		TestEqual(TEXT("ni flamme ni incantation : durée nulle"), Empty.TotalDuration, 0.f, Tolerance);
		TestEqual(TEXT("ni flamme ni incantation : pas de division par zéro"), Empty.Fill, 1.f, Tolerance);
	}
	return true;
}

#endif
