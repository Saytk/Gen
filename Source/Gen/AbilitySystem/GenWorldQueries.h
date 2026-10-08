#pragma once

#include "CoreMinimal.h"

class AActor;
class APlayerState;
class UWorld;
struct FHitResult;
enum class EGenViewerRelation : uint8;

/**
 * Requêtes de monde partagées par les projectiles et les zones au sol.
 * Un "mur" = un objet statique ou dynamique qui bloque physiquement un personnage
 * (ni un projectile, ni un volume sans blocage Pawn).
 */
namespace GenWorldQueries
{
	/** Premier mur entre Start et End. */
	GEN_API bool FindWallHit(const UWorld* World, const FVector& Start, const FVector& End, const TArray<const AActor*>& IgnoredActors, FHitResult& OutHit);

	/**
	 * Vrai si au moins un point de la capsule de Target (centre, deux bords, haut) est visible depuis Origin.
	 * Limites assumées :
	 * - le sol est un « mur » : Origin doit être AU-DESSUS du sol (sinon, sur un sol inégal, tous les points
	 *   sont cachés). Une zone au sol relève donc son point de départ (Task 6, LineOrigin) ;
	 * - pas de point aux pieds : une cible sur un rebord au-dessus d'une explosion est protégée par l'arête ;
	 * - les bords sont pris à 80 % du rayon : une cible dont moins d'environ 20 % du rayon dépasse n'est pas touchée.
	 * Les pions ne bloquent pas la vue (seuls WorldStatic et WorldDynamic, hors projectiles et volumes sans blocage Pawn).
	 */
	GEN_API bool HasLineOfSight(const UWorld* World, const FVector& Origin, const AActor* Target, const TArray<const AActor*>& IgnoredActors);

	/** Sol sous Point (trace vers le bas sur WorldStatic) ; Point lui-même si rien n'est trouvé. */
	GEN_API FVector FindFloor(const UWorld* World, const FVector& Point, const TArray<const AActor*>& IgnoredActors);

	/**
	 * Point de vue du joueur local de World (premier contrôleur) sur un effet de SourceState / SourceTeam :
	 * comparé par PlayerState (juste quand le joueur local est mort ou entre deux possessions). Sans joueur
	 * local (serveur dédié) : équipe GenNoTeam, donc ennemi ou neutre ; les appelants ne dessinent rien là.
	 */
	GEN_API EGenViewerRelation GetLocalViewerRelation(const UWorld* World, const APlayerState* SourceState, uint8 SourceTeam);
}
