#pragma once

#include "CoreMinimal.h"

/** Valeurs de départ de Curffe (spec Docs/Design/Champions/Curffe/Curffe.md, à régler en playtest). */
namespace CurffeTuning
{
	/** Flammes du Foyer. */
	inline constexpr int32 MaxFlames = 5;

	/** Une flamme regagnée passivement toutes les N secondes. */
	inline constexpr float FlameRegenPeriod = 3.f;
}
