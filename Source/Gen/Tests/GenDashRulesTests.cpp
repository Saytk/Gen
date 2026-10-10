#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AbilitySystem/GenDashRules.h"

/**
 * Gen.Dash.Zigzag : trajet de la ruée en zigzag (Flame Dash, Curffe.md « Space: Flame Dash ») : 1 + Fed segments,
 * 3 m puis 2.5 m chacun, le premier le long de la visée, puis gauche, droite, gauche à ±30°.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGenDashZigzagTest, "Gen.Dash.Zigzag",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FGenDashZigzagTest::RunTest(const FString& Parameters)
{
	GenDashRules::FZigzagParams P;
	P.BaseDistance = 300.f;
	P.SegmentDistance = 250.f;
	P.ZigzagAngle = 30.f;
	P.MaxSegments = 4;
	const FVector Start(100.f, 200.f, 90.f);

	// Nombre de segments : 1 + Fed, borné
	TestEqual(TEXT("0 flamme : 1 segment"), GenDashRules::GetSegmentCount(0, 4), 1);
	TestEqual(TEXT("3 flammes : 4 segments"), GenDashRules::GetSegmentCount(3, 4), 4);
	TestEqual(TEXT("au-delà du plafond : borné"), GenDashRules::GetSegmentCount(7, 4), 4);
	TestEqual(TEXT("compte négatif : 1 segment"), GenDashRules::GetSegmentCount(-2, 4), 1);

	// 0 flamme : une ruée de 3 m selon la visée
	const TArray<FVector> Zero = GenDashRules::ComputeZigzag(Start, 0.f, 0, P);
	if (TestEqual(TEXT("0 flamme : un point"), Zero.Num(), 1))
	{
		TestTrue(TEXT("0 flamme : 3 m devant"), Zero[0].Equals(Start + FVector(300.f, 0.f, 0.f), 0.01));
	}

	// 3 flammes, visée +Y (lacet 90°) : longueurs, alternance gauche/droite, Z constant
	const float AimYaw = 90.f;
	const TArray<FVector> Three = GenDashRules::ComputeZigzag(Start, AimYaw, 3, P);
	if (TestEqual(TEXT("3 flammes : 4 points"), Three.Num(), 4))
	{
		const FVector Aim = FRotator(0.f, AimYaw, 0.f).Vector();
		FVector From = Start;
		for (int32 Index = 0; Index < Three.Num(); ++Index)
		{
			const FVector Segment = Three[Index] - From;
			const float Expected = Index == 0 ? 300.f : 250.f;
			TestEqual(*FString::Printf(TEXT("segment %d : longueur"), Index), static_cast<float>(Segment.Size2D()), Expected, 0.01f);
			TestEqual(*FString::Printf(TEXT("segment %d : à plat"), Index), static_cast<float>(Three[Index].Z), static_cast<float>(Start.Z), 0.01f);

			// Côté par rapport à la visée : produit vectoriel (Z < 0 = à gauche dans le repère d'Unreal, vu de dessus)
			const float Side = static_cast<float>(FVector::CrossProduct(Aim, Segment.GetSafeNormal2D()).Z);
			const float Angle = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(static_cast<float>(FVector::DotProduct(Aim, Segment.GetSafeNormal2D())), -1.f, 1.f)));
			if (Index == 0)
			{
				TestEqual(TEXT("segment 0 : le long de la visée"), Angle, 0.f, 0.1f);
			}
			else
			{
				TestEqual(*FString::Printf(TEXT("segment %d : ±30°"), Index), Angle, 30.f, 0.1f);
				const bool bLeft = Side < 0.f;
				TestEqual(*FString::Printf(TEXT("segment %d : %s"), Index, Index % 2 == 1 ? TEXT("à gauche") : TEXT("à droite")), bLeft, Index % 2 == 1);
			}
			From = Three[Index];
		}

		// Arrivée : 3 m selon la visée, puis 2.5 m à gauche, à droite, à gauche (deux segments à gauche, un à droite)
		const FVector Left = FRotator(0.f, AimYaw - 30.f, 0.f).Vector();
		const FVector Right = FRotator(0.f, AimYaw + 30.f, 0.f).Vector();
		const FVector ExpectedEnd = Start + Aim * 300.f + Left * 500.f + Right * 250.f;
		TestTrue(TEXT("3 flammes : point d'arrivée"), Three.Last().Equals(ExpectedEnd, 0.01));
		TestEqual(TEXT("trajet nominal : 10,5 m"), GenDashRules::GetPathLength(3, P), 1050.f, 0.01f);
	}

	// Lacet par segment (gauche = lacet - angle) et durées à vitesse constante
	TestEqual(TEXT("segment 1 : gauche"), GenDashRules::GetSegmentYaw(10.f, 1, 30.f), -20.f, 0.001f);
	TestEqual(TEXT("segment 2 : droite"), GenDashRules::GetSegmentYaw(10.f, 2, 30.f), 40.f, 0.001f);
	TestEqual(TEXT("segment nominal : SegmentDuration"), GenDashRules::GetSegmentDuration(250.f, 250.f, 0.12f), 0.12f, 0.0001f);
	TestEqual(TEXT("segment coupé de moitié : moitié du temps"), GenDashRules::GetSegmentDuration(125.f, 250.f, 0.12f), 0.06f, 0.0001f);
	TestTrue(TEXT("segment nul : durée minimale"), GenDashRules::GetSegmentDuration(0.f, 250.f, 0.12f) > 0.f);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
