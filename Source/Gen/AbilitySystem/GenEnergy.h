#pragma once

#include "CoreMinimal.h"

/** Règles pures de l'énergie (guidelines §4.1). */
namespace GenEnergy
{
	/** L'énergie suffit-elle pour un sort qui coûte Cost ? (tolérance flottante : 100 = 100). */
	inline bool CanAfford(float Energy, float Cost)
	{
		return Cost <= 0.f || Energy + KINDA_SMALL_NUMBER >= Cost;
	}
}
