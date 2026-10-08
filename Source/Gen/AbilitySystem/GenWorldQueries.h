#pragma once

#include "CoreMinimal.h"

class AActor;
class UWorld;
struct FHitResult;

/**
 * Requêtes de monde partagées par les projectiles et les zones au sol.
 * Un "mur" = un objet statique ou dynamique qui bloque physiquement un personnage
 * (ni un projectile, ni un volume sans blocage Pawn).
 */
namespace GenWorldQueries
{
	/** Premier mur entre Start et End. */
	GEN_API bool FindWallHit(const UWorld* World, const FVector& Start, const FVector& End, const TArray<const AActor*>& IgnoredActors, FHitResult& OutHit);

	/** Vrai si au moins un point de la capsule de Target (centre, deux bords, haut) est visible depuis Origin. */
	GEN_API bool HasLineOfSight(const UWorld* World, const FVector& Origin, const AActor* Target, const TArray<const AActor*>& IgnoredActors);

	/** Sol sous Point (trace vers le bas sur WorldStatic) ; Point lui-même si rien n'est trouvé. */
	GEN_API FVector FindFloor(const UWorld* World, const FVector& Point, const TArray<const AActor*>& IgnoredActors);
}
