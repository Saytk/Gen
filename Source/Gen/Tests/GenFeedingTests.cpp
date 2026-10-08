#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AbilitySystem/GenFeeding.h"
#include "AbilitySystem/GenKnockback.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGenFeedingLimitTest, "Gen.Feeding.FeedLimit",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FGenFeedingLimitTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("5 flammes, max 5"), GenFeeding::GetFeedLimit(5, 5.f), 5);
	TestEqual(TEXT("2 flammes dispo"), GenFeeding::GetFeedLimit(5, 2.f), 2);
	TestEqual(TEXT("valeur non entière arrondie vers le bas"), GenFeeding::GetFeedLimit(5, 2.5f), 2);
	TestEqual(TEXT("aucune flamme"), GenFeeding::GetFeedLimit(5, 0.f), 0);
	TestEqual(TEXT("ressource négative"), GenFeeding::GetFeedLimit(5, -3.f), 0);
	TestEqual(TEXT("plus de ressource que le max"), GenFeeding::GetFeedLimit(5, 8.f), 5);
	TestEqual(TEXT("sort non nourrissable"), GenFeeding::GetFeedLimit(0, 5.f), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGenFeedingValidateTest, "Gen.Feeding.ServerValidation",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FGenFeedingValidateTest::RunTest(const FString& Parameters)
{
	// ValidateFedCount(ClientFed, MaxFeed, Available, ElapsedFeedTime, FeedInterval)
	TestEqual(TEXT("annonce honnête"), GenFeeding::ValidateFedCount(5, 5, 5.f, 1.0f, 0.2f), 5);
	TestEqual(TEXT("borné par la ressource serveur"), GenFeeding::ValidateFedCount(5, 5, 3.f, 1.0f, 0.2f), 3);
	TestEqual(TEXT("borné par le temps (2 intervalles + 1 de tolérance)"), GenFeeding::ValidateFedCount(5, 5, 5.f, 0.4f, 0.2f), 3);
	TestEqual(TEXT("gigue : 0.58 s pour 3 flammes accepté"), GenFeeding::ValidateFedCount(3, 5, 5.f, 0.58f, 0.2f), 3);
	TestEqual(TEXT("annonce négative"), GenFeeding::ValidateFedCount(-1, 5, 5.f, 1.0f, 0.2f), 0);
	TestEqual(TEXT("annonce au-delà du max"), GenFeeding::ValidateFedCount(9, 5, 9.f, 5.0f, 0.2f), 5);
	TestEqual(TEXT("tap immédiat"), GenFeeding::ValidateFedCount(0, 5, 5.f, 0.0f, 0.2f), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGenFeedingReportTest, "Gen.Feeding.ReportedCount",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FGenFeedingReportTest::RunTest(const FString& Parameters)
{
	// ClampReportedFed(Reported, ServerEstimate, MaxFeed, Available)
	TestEqual(TEXT("annonce égale à l'estimation"), GenFeeding::ClampReportedFed(3, 3, 5, 5.f), 3);
	TestEqual(TEXT("estimation en retard d'un tick (5 contre 4)"), GenFeeding::ClampReportedFed(5, 4, 5, 5.f), 5);
	TestEqual(TEXT("estimation en avance d'un tick"), GenFeeding::ClampReportedFed(2, 3, 5, 5.f), 2);
	TestEqual(TEXT("bluff de 5 flammes dès l'activation"), GenFeeding::ClampReportedFed(5, 0, 5, 5.f), 1);
	TestEqual(TEXT("annonce trop basse"), GenFeeding::ClampReportedFed(0, 4, 5, 5.f), 3);
	TestEqual(TEXT("borné par la ressource"), GenFeeding::ClampReportedFed(3, 2, 5, 2.f), 2);
	TestEqual(TEXT("borné par le maximum du sort"), GenFeeding::ClampReportedFed(6, 5, 5, 9.f), 5);
	TestEqual(TEXT("plus de ressource du tout"), GenFeeding::ClampReportedFed(3, 3, 5, 0.f), 0);
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
	TestTrue(TEXT("tolérance petite devant les incantations"), GenFeeding::CastTimeTolerance > 0.f && GenFeeding::CastTimeTolerance <= 0.1f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGenFeedingScaleTest, "Gen.Feeding.Scaling",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FGenFeedingScaleTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("0 flamme"), GenFeeding::ScaleByFeed(2500.f, 1600.f, 0, 5), 2500.f);
	TestEqual(TEXT("5 flammes"), GenFeeding::ScaleByFeed(2500.f, 1600.f, 5, 5), 1600.f);
	TestEqual(TEXT("au-delà du max"), GenFeeding::ScaleByFeed(2500.f, 1600.f, 10, 5), 1600.f);
	TestEqual(TEXT("MaxFeed nul"), GenFeeding::ScaleByFeed(1.f, 2.f, 3, 0), 1.f);
	TestTrue(TEXT("seuil atteint"), GenFeeding::ReachesThreshold(3, 3));
	TestFalse(TEXT("seuil non atteint"), GenFeeding::ReachesThreshold(2, 3));
	TestFalse(TEXT("seuil désactivé"), GenFeeding::ReachesThreshold(5, 0));
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

#endif
