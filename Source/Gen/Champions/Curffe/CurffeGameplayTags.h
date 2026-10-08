#pragma once

#include "NativeGameplayTags.h"

/** Tags propres à Curffe (sorts, recharges, effets). Les tags génériques sont dans GenGameplayTags. */
namespace CurffeGameplayTags
{
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Ability_Backfire);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Ability_FlamePillar);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Ability_LivingFlame);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Ability_Combustion);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Ability_Pyroblast);

	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Cooldown_Ability_Backfire);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Cooldown_Ability_FlamePillar);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Cooldown_Ability_LivingFlame);

	/** Embrasé (Combustion) : boule de feu remplacée par Pyroblast. */
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Ablaze);
	/**
	 * Forme de feu de Living Flame (avec State.Untouchable), vue par tous : point d'accroche du visuel propre à Curffe
	 * (couche de feu sur le corps, Foyer qui converge ; Curffe-Visuals §3.6, décision F22).
	 */
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_LivingFlame);

	UE_DECLARE_GAMEPLAY_TAG_EXTERN(GameplayCue_Backfire_Block);
}
