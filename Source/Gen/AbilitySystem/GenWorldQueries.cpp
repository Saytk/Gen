#include "AbilitySystem/GenWorldQueries.h"

#include "AbilitySystem/GenAreaRules.h"
#include "Actors/GenProjectile.h"
#include "CollisionQueryParams.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/HitResult.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

namespace GenWorldQueries
{
	namespace
	{
		/** Les bords testés sont un peu à l'intérieur de la capsule (pas de "touché" en frôlant un coin). */
		constexpr float SampleInset = 0.8f;
		constexpr float FloorTraceUp = 50.f;
		constexpr float FloorTraceDown = 1000.f;

		FCollisionQueryParams MakeParams(const TArray<const AActor*>& IgnoredActors)
		{
			FCollisionQueryParams Params(SCENE_QUERY_STAT(GenWorldQuery), false);
			for (const AActor* Actor : IgnoredActors)
			{
				if (Actor)
				{
					Params.AddIgnoredActor(Actor);
				}
			}
			return Params;
		}
	}

	bool FindWallHit(const UWorld* World, const FVector& Start, const FVector& End, const TArray<const AActor*>& IgnoredActors, FHitResult& OutHit)
	{
		if (!World)
		{
			return false;
		}

		FCollisionObjectQueryParams WallObjects;
		WallObjects.AddObjectTypesToQuery(ECC_WorldStatic);
		WallObjects.AddObjectTypesToQuery(ECC_WorldDynamic);

		TArray<FHitResult> Hits;
		World->LineTraceMultiByObjectType(Hits, Start, End, WallObjects, MakeParams(IgnoredActors));
		for (const FHitResult& Hit : Hits)
		{
			// Un autre projectile ou un volume qui ne bloque pas les Pawns n'est pas un mur (comme l'ancien
			// AGenProjectile::FindWallHit : on garde le test explicite, un Blueprint de projectile peut bloquer les Pawns)
			const UPrimitiveComponent* HitComponent = Hit.GetComponent();
			if (Cast<AGenProjectile>(Hit.GetActor()) || !HitComponent || HitComponent->GetCollisionResponseToChannel(ECC_Pawn) != ECR_Block)
			{
				continue;
			}
			OutHit = Hit;
			return true;
		}
		return false;
	}

	bool HasLineOfSight(const UWorld* World, const FVector& Origin, const AActor* Target, const TArray<const AActor*>& IgnoredActors)
	{
		if (!World || !Target)
		{
			return false;
		}

		float Radius = 0.f;
		float HalfHeight = 0.f;
		Target->GetSimpleCollisionCylinder(Radius, HalfHeight);

		FHitResult WallHit;
		for (const FVector& Sample : GenAreaRules::GetLineOfSightSamples(Origin, Target->GetActorLocation(), Radius * SampleInset, HalfHeight * SampleInset))
		{
			if (!FindWallHit(World, Origin, Sample, IgnoredActors, WallHit))
			{
				return true;
			}
		}
		return false;
	}

	FVector FindFloor(const UWorld* World, const FVector& Point, const TArray<const AActor*>& IgnoredActors)
	{
		FVector Floor = Point;
		TryFindFloor(World, Point, IgnoredActors, Floor);
		return Floor;
	}

	bool TryFindFloor(const UWorld* World, const FVector& Point, const TArray<const AActor*>& IgnoredActors, FVector& OutFloor)
	{
		if (!World)
		{
			return false;
		}

		FHitResult Hit;
		const FCollisionObjectQueryParams Floors(ECC_WorldStatic);
		if (World->LineTraceSingleByObjectType(Hit, Point + FVector(0.f, 0.f, FloorTraceUp), Point - FVector(0.f, 0.f, FloorTraceDown), Floors, MakeParams(IgnoredActors)))
		{
			OutFloor = FVector(Hit.ImpactPoint);
			return true;
		}
		return false;
	}
}
