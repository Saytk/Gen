#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AbilitySystem/Abilities/GenGA_Projectile.h"
#include "AbilitySystem/Abilities/GenGameplayAbility.h"
#include "AbilitySystem/GenCastBarRules.h"
#include "Character/GenTrainingDummy.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Tests/GenTestWorld.h"

using GenTestWorld::FScopedTestWorld;

/**
 * Barre de cast portée par le personnage (FGenCastInfo) : corrections du compte à la fin du nourrissage
 * (jamais de recul sauf compte final) et composition de la disposition (GetCastBarLayout).
 * Monde standalone : l'horloge des incantations est le temps du monde (pas de GameState).
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGenCastInfoFeedEndTest, "Gen.CastBar.MarkFeedEnded",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FGenCastInfoFeedEndTest::RunTest(const FString& Parameters)
{
	FScopedTestWorld TestWorld;
	AGenTrainingDummy* Caster = TestWorld.SpawnDummy();
	if (!TestNotNull(TEXT("mannequin"), Caster))
	{
		return false;
	}

	UClass* Fed = UGenGA_Projectile::StaticClass();
	UClass* Other = UGenGameplayAbility::StaticClass();

	// 3 crans de 0.3 s puis 0.5 s d'incantation
	Caster->StartFeedCast(Fed, 3, 0.3f, 0.5f);
	TestEqual(TEXT("durée maximale = 3 x 0.3 + 0.5"), Caster->GetCastInfo().Duration, 1.4f, KINDA_SMALL_NUMBER);
	TestEqual(TEXT("nourrissage en cours"), Caster->GetCastInfo().FeedEndTime, 0.f);

	TestWorld.Advance(0.65f);

	Caster->MarkFeedEnded(Other, 1);
	TestEqual(TEXT("barre d'un autre sort : ignoré"), Caster->GetCastInfo().FeedEndTime, 0.f);

	Caster->MarkFeedEnded(Fed, 2);
	const float EndTime = Caster->GetCastInfo().FeedEndTime;
	TestTrue(TEXT("fin du nourrissage datée"), EndTime > 0.f);
	TestEqual(TEXT("2 flammes"), static_cast<int32>(Caster->GetCastInfo().FedCount), 2);

	TestWorld.Advance(0.1f);

	Caster->MarkFeedEnded(Fed, 1);
	TestEqual(TEXT("correction plus basse : le compteur ne recule pas"), static_cast<int32>(Caster->GetCastInfo().FedCount), 2);
	TestEqual(TEXT("correction : le repli garde son heure"), Caster->GetCastInfo().FeedEndTime, EndTime);

	Caster->MarkFeedEnded(Fed, 3);
	TestEqual(TEXT("correction plus haute acceptée"), static_cast<int32>(Caster->GetCastInfo().FedCount), 3);
	TestEqual(TEXT("correction plus haute : même heure"), Caster->GetCastInfo().FeedEndTime, EndTime);

	Caster->MarkFeedEnded(Fed, 1, /*bFinal*/ true);
	TestEqual(TEXT("compte final validé au lancer : peut descendre"), static_cast<int32>(Caster->GetCastInfo().FedCount), 1);

	Caster->MarkFeedEnded(Fed, 9, /*bFinal*/ true);
	TestEqual(TEXT("borné aux crans de la barre"), static_cast<int32>(Caster->GetCastInfo().FedCount), 3);

	Caster->StopCast(Other);
	TestTrue(TEXT("StopCast d'un autre sort : la barre reste"), Caster->GetCastInfo().IsCasting());
	Caster->StopCast(Fed);
	TestFalse(TEXT("StopCast : plus d'incantation"), Caster->GetCastInfo().IsCasting());

	Caster->MarkFeedEnded(Fed, 2);
	TestEqual(TEXT("après StopCast : ignoré"), Caster->GetCastInfo().FeedEndTime, 0.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGenCastInfoLayoutTest, "Gen.CastBar.CharacterLayout",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FGenCastInfoLayoutTest::RunTest(const FString& Parameters)
{
	FScopedTestWorld TestWorld;
	AGenTrainingDummy* Caster = TestWorld.SpawnDummy();
	if (!TestNotNull(TEXT("mannequin"), Caster))
	{
		return false;
	}

	UClass* Fed = UGenGA_Projectile::StaticClass();
	GenCastBar::FLayout Layout;

	TestFalse(TEXT("aucune incantation"), Caster->GetCastBarLayout(Layout));
	TestEqual(TEXT("progression -1 sans incantation"), Caster->GetCastProgress(), -1.f);

	// Nourrissage : 3 crans, compteur = flammes nourries en direct (FedResource)
	Caster->StartFeedCast(Fed, 3, 0.3f, 0.5f);
	Caster->SetFedResource(Caster, 2);
	TestWorld.Advance(0.65f);
	if (!TestTrue(TEXT("nourrissage : barre"), Caster->GetCastBarLayout(Layout)))
	{
		return false;
	}
	TestTrue(TEXT("nourrissage : sort nourri"), Layout.bFed);
	TestEqual(TEXT("nourrissage : longueur maximale"), Layout.TotalDuration, 1.4f, 1.e-3f);
	TestEqual(TEXT("nourrissage : 3 crans"), Layout.Ticks.Num(), 3);
	if (Layout.Ticks.Num() == 3)
	{
		TestEqual(TEXT("1er cran à 0.3 / 1.4"), Layout.Ticks[0], 0.3f / 1.4f, 1.e-3f);
		TestEqual(TEXT("3e cran à 0.9 / 1.4"), Layout.Ticks[2], 0.9f / 1.4f, 1.e-3f);
	}
	TestEqual(TEXT("nourrissage : compteur = FedResource"), Layout.Counter, 2);

	// Fin du nourrissage à 2 : le compteur vient de CastInfo, plus de FedResource ; incantation = Duration - 3 x 0.3
	Caster->MarkFeedEnded(Fed, 2);
	Caster->SetFedResource(Caster, 0);
	TestWorld.Advance(0.2f); // repli (0.1 s) terminé
	Caster->GetCastBarLayout(Layout);
	TestEqual(TEXT("après repli : 2 x 0.3 + 0.5"), Layout.TotalDuration, 1.1f, 1.e-3f);
	TestEqual(TEXT("après repli : 2 crans"), Layout.Ticks.Num(), 2);
	TestEqual(TEXT("après repli : compteur = compte final"), Layout.Counter, 2);
	TestTrue(TEXT("après repli : remplissage au-delà des crans"), Layout.Fill > 0.6f / 1.1f);

	// Incantation normale : pas de crans ni de compteur
	Caster->StartCast(Fed, 0.5f);
	Caster->GetCastBarLayout(Layout);
	TestFalse(TEXT("incantation normale : pas nourrie"), Layout.bFed);
	TestEqual(TEXT("incantation normale : durée"), Layout.TotalDuration, 0.5f, 1.e-3f);
	TestEqual(TEXT("incantation normale : aucun cran"), Layout.Ticks.Num(), 0);
	TestEqual(TEXT("incantation normale : StartCast efface le nourrissage précédent"), static_cast<int32>(Caster->GetCastInfo().FeedSlots), 0);

	Caster->StopCast(Fed);
	TestFalse(TEXT("fin : plus de barre"), Caster->GetCastBarLayout(Layout));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGenCastInfoChannelTest, "Gen.CastBar.CharacterChannel",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FGenCastInfoChannelTest::RunTest(const FString& Parameters)
{
	FScopedTestWorld TestWorld;
	AGenTrainingDummy* Caster = TestWorld.SpawnDummy();
	if (!TestNotNull(TEXT("mannequin"), Caster))
	{
		return false;
	}

	UClass* Window = UGenGA_Projectile::StaticClass();
	TestEqual(TEXT("sans incantation : horloge à 0"), Caster->GetCastElapsedFraction(), 0.f);

	// Une incantation en cours est remplacée par la canalisation (même logique que StartCast)
	Caster->StartFeedCast(Window, 3, 0.3f, 0.5f);
	Caster->StartChannel(Window, 1.2f);
	TestTrue(TEXT("canalisation"), Caster->GetCastInfo().bChannel);
	TestEqual(TEXT("plus de crans"), static_cast<int32>(Caster->GetCastInfo().FeedSlots), 0);

	TestWorld.Advance(0.6f);
	GenCastBar::FLayout Layout;
	if (!TestTrue(TEXT("barre"), Caster->GetCastBarLayout(Layout)))
	{
		return false;
	}
	TestTrue(TEXT("se vide"), Layout.bDrain);
	TestEqual(TEXT("moitié restante"), Layout.Fill, 0.5f, 0.03f);
	TestEqual(TEXT("moitié écoulée"), Caster->GetCastElapsedFraction(), 0.5f, 0.03f);

	Caster->StopCast(Window);
	TestFalse(TEXT("StopCast la termine"), Caster->GetCastInfo().IsCasting());
	TestFalse(TEXT("plus de canalisation"), Caster->GetCastInfo().bChannel);
	TestEqual(TEXT("horloge à 0"), Caster->GetCastElapsedFraction(), 0.f);

	// Une incantation normale suivante n'hérite pas du drapeau
	Caster->StartCast(Window, 0.5f);
	TestFalse(TEXT("StartCast : pas une canalisation"), Caster->GetCastInfo().bChannel);

	// Orientation (bug du 2026-10-09 : pendant la fenêtre du contre, ZQSD retournait le personnage vers son déplacement).
	// Par défaut une canalisation suit le déplacement (Living Flame) ; une posture (contre) reste face à la visée
	const UCharacterMovementComponent* Movement = Caster->GetCharacterMovement();
	Caster->StartChannel(Window, 1.2f);
	TestTrue(TEXT("canalisation : face au déplacement"), Movement->bOrientRotationToMovement && !Movement->bUseControllerDesiredRotation);
	Caster->StartChannel(Window, 1.2f, /*bFaceAim*/ true);
	TestTrue(TEXT("posture : face à la visée"), !Movement->bOrientRotationToMovement && Movement->bUseControllerDesiredRotation);
	Caster->StopCast(Window);
	TestTrue(TEXT("fin de posture : de nouveau face au déplacement"), Movement->bOrientRotationToMovement && !Movement->bUseControllerDesiredRotation);
	return true;
}

#endif
