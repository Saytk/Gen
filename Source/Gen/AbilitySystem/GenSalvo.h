#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectKey.h"

/**
 * Salve de projectiles tirés ensemble (anneau du Bond météore) : une cible n'est touchée que par un
 * projectile de la salve, les suivants la traversent. Partagée par les projectiles (serveur uniquement).
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
