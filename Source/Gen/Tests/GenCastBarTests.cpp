#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AbilitySystem/GenCastBarRules.h"
#include "AbilitySystem/GenFeeding.h"
#include "Champions/Curffe/CurffeTuning.h"

namespace GenCastBarTests
{
	constexpr float Tolerance = 1.e-4f;

	/** Grande boule de feu type : 3 flammes, une toutes les 0.3 s, 0.5 s d'incantation, appui à 10 s. */
	GenCastBar::FParams MakeFeed(int32 Slots = 3, float Interval = 0.3f, float CastTime = 0.5f)
	{
		GenCastBar::FParams Params;
		Params.FeedSlots = Slots;
		Params.FeedInterval = Interval;
		Params.CastTime = CastTime;
		Params.StartTime = 10.f;
		return Params;
	}

	GenCastBar::FParams MakeEnded(float EndTime, int32 Fed, int32 Slots = 3, float Interval = 0.3f, float CastTime = 0.5f)
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
	TestEqual(TEXT("longueur = 3 x 0.3 + 0.5"), AtPress.TotalDuration, 1.4f, Tolerance);
	TestEqual(TEXT("vide à l'appui"), AtPress.Fill, 0.f, Tolerance);
	TestEqual(TEXT("compteur à 0"), AtPress.Counter, 0);
	if (TestEqual(TEXT("un cran par flamme disponible"), AtPress.Ticks.Num(), 3))
	{
		for (int32 K = 1; K <= 3; ++K)
		{
			TestEqual(*FString::Printf(TEXT("cran %d à k x 0.3 / 1.4"), K), AtPress.Ticks[K - 1], K * 0.3f / 1.4f, Tolerance);
		}
	}

	// Pendant le nourrissage, FedCount = flammes réellement nourries (en direct) : le compteur les montre
	// telles quelles, sans les déduire du temps, pour ne jamais reculer au relâché
	GenCastBar::FParams Live = Params;
	Live.FedCount = 1;
	const GenCastBar::FLayout Mid = GenCastBar::ComputeLayout(Live, 10.45f);
	TestEqual(TEXT("remplissage en temps réel"), Mid.Fill, 0.45f / 1.4f, Tolerance);
	TestEqual(TEXT("1 flamme nourrie"), Mid.Counter, 1);
	TestEqual(TEXT("les crans ne bougent pas pendant le nourrissage"), Mid.Ticks.Num(), 3);

	TestEqual(TEXT("tick en retard : le compteur attend la vraie flamme"), GenCastBar::ComputeLayout(Live, 10.65f).Counter, 1);
	TestEqual(TEXT("le remplissage, lui, suit le temps"), GenCastBar::ComputeLayout(Live, 10.65f).Fill, 0.65f / 1.4f, Tolerance);

	Live.FedCount = 7;
	TestEqual(TEXT("compte en direct borné aux emplacements"), GenCastBar::ComputeLayout(Live, 10.45f).Counter, 3);
	Live.FedCount = -1;
	TestEqual(TEXT("compte en direct négatif"), GenCastBar::ComputeLayout(Live, 10.45f).Counter, 0);

	// Fin du nourrissage pas encore connue (signal du client en route) : la barre continue
	Live.FedCount = 3;
	const GenCastBar::FLayout Late = GenCastBar::ComputeLayout(Live, 11.2f);
	TestEqual(TEXT("compteur au maximum"), Late.Counter, 3);
	TestEqual(TEXT("la barre continue dans l'incantation"), Late.Fill, 1.2f / 1.4f, Tolerance);
	TestEqual(TEXT("remplissage borné à 1"), GenCastBar::ComputeLayout(Params, 12.f).Fill, 1.f, Tolerance);
	TestEqual(TEXT("horloge en retard sur le début : vide"), GenCastBar::ComputeLayout(Params, 9.9f).Fill, 0.f, Tolerance);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGenCastBarCurffeHearthTest, "Gen.CastBar.CurffeHearth",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FGenCastBarCurffeHearthTest::RunTest(const FString& Parameters)
{
	using namespace GenCastBarTests;

	// Crans posés à l'appui comme le fait UGenGA_Cast : min(MaxFeed, flammes du Foyer)
	auto SlotsFor = [](float Flames) { return GenFeeding::GetFeedLimit(CurffeTuning::MaxFeedPerSpell, Flames); };

	// Foyer plein (5 flammes) : 3 crans, jamais 5
	{
		const GenCastBar::FParams Full = MakeFeed(SlotsFor(CurffeTuning::MaxFlames), CurffeTuning::FeedInterval);
		const GenCastBar::FLayout Layout = GenCastBar::ComputeLayout(Full, 10.f);
		TestEqual(TEXT("Foyer plein : 3 crans"), Layout.Ticks.Num(), 3);
		TestEqual(TEXT("Foyer plein : barre max 1.4 s (0.5 + 3 x 0.3)"), Layout.TotalDuration, 1.4f, Tolerance);

		// Maintenu bien après le 3e seuil : toujours 3 crans, compteur à 3
		GenCastBar::FParams Held = Full;
		Held.FedCount = 3;
		const GenCastBar::FLayout HeldLayout = GenCastBar::ComputeLayout(Held, 10.95f);
		TestEqual(TEXT("maintien prolongé : 3 crans"), HeldLayout.Ticks.Num(), 3);
		TestEqual(TEXT("maintien prolongé : compteur 3"), HeldLayout.Counter, 3);
	}

	// Moins de flammes que de seuils : autant de crans que de flammes
	TestEqual(TEXT("2 flammes : 2 crans"), GenCastBar::ComputeLayout(MakeFeed(SlotsFor(2.f), CurffeTuning::FeedInterval), 10.f).Ticks.Num(), 2);
	TestEqual(TEXT("1 flamme : 1 cran"), GenCastBar::ComputeLayout(MakeFeed(SlotsFor(1.f), CurffeTuning::FeedInterval), 10.f).Ticks.Num(), 1);
	TestFalse(TEXT("Foyer vide : incantation normale"), GenCastBar::ComputeLayout(MakeFeed(SlotsFor(0.f), CurffeTuning::FeedInterval), 10.f).bFed);

	// Combustion : même nombre de crans, deux fois plus serrés dans le temps
	const GenCastBar::FLayout Fast = GenCastBar::ComputeLayout(MakeFeed(SlotsFor(CurffeTuning::MaxFlames), CurffeTuning::FastFeedInterval), 10.f);
	TestEqual(TEXT("Combustion : 3 crans"), Fast.Ticks.Num(), 3);
	TestEqual(TEXT("Combustion : barre max 0.95 s"), Fast.TotalDuration, 0.95f, Tolerance);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGenCastBarReleaseTest, "Gen.CastBar.Release",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FGenCastBarReleaseTest::RunTest(const FString& Parameters)
{
	using namespace GenCastBarTests;

	// Relâché à 10.75 s avec 2 flammes : total visé 2 x 0.3 + 0.5 = 1.1 s
	const GenCastBar::FParams Params = MakeEnded(10.75f, 2);

	const GenCastBar::FLayout AtRelease = GenCastBar::ComputeLayout(Params, 10.75f);
	TestEqual(TEXT("au relâché : longueur de nourrissage"), AtRelease.TotalDuration, 1.4f, Tolerance);
	TestEqual(TEXT("le temps après le dernier seuil est abandonné"), AtRelease.Fill, 0.6f / 1.4f, Tolerance);
	TestEqual(TEXT("compteur = flammes nourries"), AtRelease.Counter, 2);
	if (TestEqual(TEXT("crans 1..N seulement"), AtRelease.Ticks.Num(), 2))
	{
		TestEqual(TEXT("cran 1"), AtRelease.Ticks[0], 0.3f / 1.4f, Tolerance);
		TestEqual(TEXT("cran 2"), AtRelease.Ticks[1], 0.6f / 1.4f, Tolerance);
	}

	const GenCastBar::FLayout Collapsing = GenCastBar::ComputeLayout(Params, 10.8f);
	TestEqual(TEXT("repli à mi-course"), Collapsing.TotalDuration, 1.25f, Tolerance);
	TestEqual(TEXT("remplissage pendant le repli"), Collapsing.Fill, 0.65f / 1.25f, Tolerance);
	if (TestEqual(TEXT("2 crans pendant le repli"), Collapsing.Ticks.Num(), 2))
	{
		TestEqual(TEXT("cran 2 suit le repli"), Collapsing.Ticks[1], 0.6f / 1.25f, Tolerance);
	}

	const GenCastBar::FLayout Collapsed = GenCastBar::ComputeLayout(Params, 10.85f);
	TestEqual(TEXT("repli terminé en 100 ms"), Collapsed.TotalDuration, 1.1f, Tolerance);
	TestEqual(TEXT("la même barre continue"), Collapsed.Fill, 0.7f / 1.1f, Tolerance);
	if (TestEqual(TEXT("2 crans après le repli"), Collapsed.Ticks.Num(), 2))
	{
		TestEqual(TEXT("crans recalés"), Collapsed.Ticks[0], 0.3f / 1.1f, Tolerance);
	}

	TestEqual(TEXT("pleine à la fin de l'incantation"), GenCastBar::ComputeLayout(Params, 11.25f).Fill, 1.f, Tolerance);
	TestEqual(TEXT("bornée après"), GenCastBar::ComputeLayout(Params, 11.5f).Fill, 1.f, Tolerance);
	TestEqual(TEXT("compteur figé"), GenCastBar::ComputeLayout(Params, 11.5f).Counter, 2);

	// Horloge locale un peu avant la fin répliquée : rien ne recule ni n'avance
	const GenCastBar::FLayout Early = GenCastBar::ComputeLayout(Params, 10.7f);
	TestEqual(TEXT("avant la fin : pas de repli"), Early.TotalDuration, 1.4f, Tolerance);
	TestEqual(TEXT("avant la fin : seuil N"), Early.Fill, 0.6f / 1.4f, Tolerance);

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
		TestFalse(TEXT("intervalle nul : barre normale"), GenCastBar::ComputeLayout(MakeFeed(3, 0.f), 10.25f).bFed);
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

		// Au milieu du repli (alpha = 0.5) : total entre 1.4 et 0.5 s
		const GenCastBar::FLayout Collapsing = GenCastBar::ComputeLayout(Params, 10.15f);
		TestEqual(TEXT("N = 0, mi-repli : total"), Collapsing.TotalDuration, 0.95f, Tolerance);
		TestEqual(TEXT("N = 0, mi-repli : remplissage = p / total"), Collapsing.Fill, 0.05f / 0.95f, Tolerance);
		TestEqual(TEXT("N = 0, mi-repli : aucun cran"), Collapsing.Ticks.Num(), 0);
		TestEqual(TEXT("N = 0, mi-repli : compteur"), Collapsing.Counter, 0);
	}

	// N = FeedSlots : rien à replier
	{
		const GenCastBar::FParams Params = MakeEnded(10.9f, 3);
		const GenCastBar::FLayout Layout = GenCastBar::ComputeLayout(Params, 10.95f);
		TestEqual(TEXT("N max : longueur inchangée"), Layout.TotalDuration, 1.4f, Tolerance);
		TestEqual(TEXT("N max : remplissage continu"), Layout.Fill, 0.95f / 1.4f, Tolerance);
		TestEqual(TEXT("N max : tous les crans"), Layout.Ticks.Num(), 3);
		TestEqual(TEXT("N max : compteur"), Layout.Counter, 3);
		TestEqual(TEXT("compte annoncé au-delà des emplacements"), GenCastBar::ComputeLayout(MakeEnded(10.9f, 5), 10.95f).Counter, 3);
	}

	// Relâché pile sur un seuil : pas de saut du remplissage
	{
		const float Before = GenCastBar::ComputeLayout(MakeFeed(), 10.6f).Fill;
		const float After = GenCastBar::ComputeLayout(MakeEnded(10.6f, 2), 10.6f).Fill;
		TestEqual(TEXT("relâché sur un seuil : continuité"), After, Before, Tolerance);
	}

	// CastTime = 0 : la barre se termine sur le dernier seuil
	{
		const GenCastBar::FLayout Feeding = GenCastBar::ComputeLayout(MakeFeed(3, 0.3f, 0.f), 10.15f);
		TestEqual(TEXT("sans incantation : longueur"), Feeding.TotalDuration, 0.9f, Tolerance);
		TestEqual(TEXT("sans incantation : remplissage"), Feeding.Fill, 0.15f / 0.9f, Tolerance);
		TestEqual(TEXT("sans incantation : 3 crans"), Feeding.Ticks.Num(), 3);

		const GenCastBar::FParams Ended = MakeEnded(10.45f, 1, 3, 0.3f, 0.f);
		TestEqual(TEXT("sans incantation : repli"), GenCastBar::ComputeLayout(Ended, 10.5f).TotalDuration, 0.6f, Tolerance);
		TestEqual(TEXT("sans incantation : remplissage pendant le repli"), GenCastBar::ComputeLayout(Ended, 10.5f).Fill, 0.5f, Tolerance);
		TestEqual(TEXT("sans incantation : pleine après le repli"), GenCastBar::ComputeLayout(Ended, 10.55f).Fill, 1.f, Tolerance);

		const GenCastBar::FLayout Empty = GenCastBar::ComputeLayout(MakeEnded(10.1f, 0, 3, 0.3f, 0.f), 10.3f);
		TestEqual(TEXT("ni flamme ni incantation : durée nulle"), Empty.TotalDuration, 0.f, Tolerance);
		TestEqual(TEXT("ni flamme ni incantation : pas de division par zéro"), Empty.Fill, 1.f, Tolerance);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGenCastBarChannelTest, "Gen.CastBar.Channel",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FGenCastBarChannelTest::RunTest(const FString& Parameters)
{
	// Fenêtre de contre (Backfire) : 1.2 s
	GenCastBar::FParams P;
	P.CastTime = 1.2f;
	P.StartTime = 10.f;
	P.bChannel = true;
	const GenCastBar::FLayout Start = GenCastBar::ComputeLayout(P, 10.f);
	const GenCastBar::FLayout Mid = GenCastBar::ComputeLayout(P, 10.6f);
	const GenCastBar::FLayout End = GenCastBar::ComputeLayout(P, 11.2f);
	const GenCastBar::FLayout After = GenCastBar::ComputeLayout(P, 12.f);
	TestTrue(TEXT("se vide (UI §4.5)"), Start.bDrain);
	TestFalse(TEXT("pas un sort nourri"), Start.bFed);
	TestEqual(TEXT("pleine au début"), Start.Fill, 1.f, 0.001f);
	TestEqual(TEXT("moitié"), Mid.Fill, 0.5f, 0.001f);
	TestEqual(TEXT("vide à la fin"), End.Fill, 0.f, 0.001f);
	TestEqual(TEXT("reste vide après"), After.Fill, 0.f, 0.001f);
	TestEqual(TEXT("ni cran"), Mid.Ticks.Num(), 0);
	TestEqual(TEXT("ni compteur"), Mid.Counter, 0);
	TestEqual(TEXT("durée = fenêtre"), Mid.TotalDuration, 1.2f, 0.001f);

	// Une canalisation n'est jamais nourrie, même si des crans traînent dans les paramètres
	P.FeedSlots = 3;
	P.FeedInterval = 0.3f;
	const GenCastBar::FLayout Ignored = GenCastBar::ComputeLayout(P, 10.6f);
	TestTrue(TEXT("crans ignorés : toujours une canalisation"), Ignored.bDrain && !Ignored.bFed && Ignored.Ticks.Num() == 0);
	TestEqual(TEXT("crans ignorés : moitié"), Ignored.Fill, 0.5f, 0.001f);

	// Durée nulle : vide
	GenCastBar::FParams Zero;
	Zero.bChannel = true;
	TestEqual(TEXT("durée nulle : vide"), GenCastBar::ComputeLayout(Zero, 5.f).Fill, 0.f, 0.001f);

	// Incantation normale : se remplit (inchangé)
	P.bChannel = false;
	P.FeedSlots = 0;
	TestFalse(TEXT("incantation : ne se vide pas"), GenCastBar::ComputeLayout(P, 10.6f).bDrain);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGenCastBarElapsedFractionTest, "Gen.CastBar.ElapsedFraction",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FGenCastBarElapsedFractionTest::RunTest(const FString& Parameters)
{
	// Horloge des télégraphes : grandit de 0 à 1, que la barre se remplisse ou se vide
	TestEqual(TEXT("début"), GenCastBar::GetElapsedFraction(10.f, 0.5f, 10.f), 0.f, 0.0001f);
	TestEqual(TEXT("milieu"), GenCastBar::GetElapsedFraction(10.f, 0.5f, 10.25f), 0.5f, 0.0001f);
	TestEqual(TEXT("fin"), GenCastBar::GetElapsedFraction(10.f, 0.5f, 10.5f), 1.f, 0.0001f);
	TestEqual(TEXT("après : borné"), GenCastBar::GetElapsedFraction(10.f, 0.5f, 11.f), 1.f, 0.0001f);
	TestEqual(TEXT("horloge en retard : borné à 0"), GenCastBar::GetElapsedFraction(10.f, 0.5f, 9.9f), 0.f, 0.0001f);
	TestEqual(TEXT("durée nulle"), GenCastBar::GetElapsedFraction(10.f, 0.f, 11.f), 0.f, 0.0001f);

	// Canalisation : remplissage de la barre = 1 - part écoulée
	GenCastBar::FParams P;
	P.CastTime = 0.5f;
	P.StartTime = 10.f;
	P.bChannel = true;
	for (const float Now : { 10.f, 10.1f, 10.35f, 10.5f })
	{
		TestEqual(FString::Printf(TEXT("barre + horloge = 1 à %.2f"), Now),
			GenCastBar::ComputeLayout(P, Now).Fill + GenCastBar::GetElapsedFraction(P.StartTime, P.CastTime, Now), 1.f, 0.0001f);
	}
	return true;
}

#endif
