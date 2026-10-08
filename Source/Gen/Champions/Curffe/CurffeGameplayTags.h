#pragma once

#include "NativeGameplayTags.h"

/** Tags propres à Curffe (sorts, recharges, effets). Les tags génériques sont dans GenGameplayTags. */
namespace CurffeGameplayTags
{
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Ability_Backfire);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Ability_FlamePillar);

	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Cooldown_Ability_Backfire);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Cooldown_Ability_FlamePillar);

	UE_DECLARE_GAMEPLAY_TAG_EXTERN(GameplayCue_Backfire_Block);
}
