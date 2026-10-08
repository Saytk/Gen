#pragma once

#include "CoreMinimal.h"

/** Valeurs de départ de Curffe (spec Docs/Design/Champions/Curffe/Curffe.md, à régler en playtest). */
namespace CurffeTuning
{
	/** Flammes du Foyer. */
	inline constexpr int32 MaxFlames = 5;

	/** Une flamme regagnée passivement toutes les N secondes. */
	inline constexpr float FlameRegenPeriod = 3.f;

	/**
	 * Nourrissage (spec §2, décision du 2026-10-08) : au plus 3 seuils par sort, quel que soit le Foyer.
	 * Un Foyer plein paie donc un sort à 3 flammes puis un sort à 2.
	 */
	inline constexpr int32 MaxFeedPerSpell = 3;

	/** Une flamme passe dans le sort toutes les N secondes de maintien. */
	inline constexpr float FeedInterval = 0.3f;

	/** Nourrissage rapide pendant Combustion (spec §3, F). */
	inline constexpr float FastFeedInterval = 0.15f;

	/** Grande boule de feu : explosion de zone à partir de N flammes. */
	inline constexpr int32 GreatFireballSplashMinFeed = 2;

	/** Grande boule de feu : repoussement à partir de N flammes. */
	inline constexpr int32 GreatFireballKnockbackMinFeed = 3;

	static_assert(MaxFeedPerSpell <= MaxFlames, "Un sort ne peut pas absorber plus de flammes que le Foyer n'en contient");
	static_assert(GreatFireballSplashMinFeed <= GreatFireballKnockbackMinFeed && GreatFireballKnockbackMinFeed <= MaxFeedPerSpell,
		"Les seuils de la grande boule de feu doivent être atteignables, dans l'ordre");
}
