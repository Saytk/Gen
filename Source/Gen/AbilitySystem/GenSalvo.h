#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectKey.h"

/**
 * Salve de projectiles tirés ensemble (anneau du Bond météore) : une cible n'est touchée que par un
 * projectile de la salve, les suivants la traversent. Partagée par les projectiles (serveur uniquement).
 * - Coups directs seulement : l'éclaboussure d'un projectile de la salve ne consulte ni ne prend la salve
 *   (les boules de l'anneau n'ont pas d'éclaboussure). Une salve à éclaboussure devra la prendre aussi.
 * - Une cible qui bloque par un contre est prise quand même : une interaction par ennemi et par salve
 *   (spec Curffe, Bond météore). Une cible intouchable, traversée, ne l'est pas.
 */
struct FGenProjectileSalvo
{
	bool HasHit(const UObject* Target) const
	{
		return Target && HitTargets.Contains(FObjectKey(Target));
	}

	/** Vrai si Target n'avait pas encore été touchée par la salve (elle l'est désormais). */
	bool TryClaim(const UObject* Target)
	{
		if (!Target)
		{
			return false;
		}
		bool bAlreadyHit = false;
		HitTargets.Add(FObjectKey(Target), &bAlreadyHit);
		return !bAlreadyHit;
	}

private:
	TSet<FObjectKey> HitTargets;
};
