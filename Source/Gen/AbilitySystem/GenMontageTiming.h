#pragma once

#include "CoreMinimal.h"

/**
 * Règles pures du calage des montages sur les durées de jeu (Art Bible §8.3 : l'anticipation dure exactement le temps
 * d'incantation, jamais plus). Le minuteur du sort reste la seule source du moment du lancer (Art Bible §8.4 #4).
 */
namespace GenMontageTiming
{
	inline constexpr float MinPlayRate = 0.5f;
	inline constexpr float MaxPlayRate = 2.5f;

	/** Au-delà de ces écarts (relatifs à la vitesse attendue), le clip devrait être recalé à la main (Art Bible §8.2, étape 4). */
	inline constexpr float WarnLowRatio = 0.8f;
	inline constexpr float WarnHighRatio = 1.25f;

	/** Vitesse pour qu'un clip de AuthoredLength s dure TargetDuration s. 1 si l'une des deux est nulle. */
	inline float GetPlayRate(float AuthoredLength, float TargetDuration)
	{
		if (AuthoredLength <= KINDA_SMALL_NUMBER || TargetDuration <= KINDA_SMALL_NUMBER)
		{
			return 1.f;
		}
		return FMath::Clamp(AuthoredLength / TargetDuration, MinPlayRate, MaxPlayRate);
	}

	/** Vrai si PlayRate s'écarte trop de la vitesse attendue (1, ou 2 pour le nourrissage rapide voulu). */
	inline bool ShouldWarn(float PlayRate, float ExpectedRate = 1.f)
	{
		const float Ratio = ExpectedRate > KINDA_SMALL_NUMBER ? PlayRate / ExpectedRate : PlayRate;
		return Ratio < WarnLowRatio || Ratio > WarnHighRatio;
	}
}
