#include "Champions/Curffe/CurffeGameplayTags.h"

namespace CurffeGameplayTags
{
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Ability_Backfire, "Ability.Backfire", "Sort : retour de flamme (contre de Curffe)");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Ability_FlamePillar, "Ability.FlamePillar", "Sort : pilier de flammes (zone retardee, etourdit)");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Ability_LivingFlame, "Ability.LivingFlame", "Sort : flamme vivante (R)");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Ability_Combustion, "Ability.Combustion", "Sort : combustion (ultime)");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Ability_Pyroblast, "Ability.Pyroblast", "Sort : pyroblast (clic gauche pendant la combustion)");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Cooldown_Ability_Backfire, "Cooldown.Ability.Backfire", "Recharge du retour de flamme");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Cooldown_Ability_FlamePillar, "Cooldown.Ability.FlamePillar", "Recharge du pilier de flammes");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Cooldown_Ability_LivingFlame, "Cooldown.Ability.LivingFlame", "Recharge de la flamme vivante");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(State_Ablaze, "State.Curffe.Ablaze", "Embrase (combustion) : pyroblasts, flammes illimitees, nourrissage rapide");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(State_LivingFlame, "State.Curffe.LivingFlame", "Forme de feu de la flamme vivante (intouchable, sans sort) : visuel propre a Curffe");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(GameplayCue_Backfire_Block, "GameplayCue.Curffe.Backfire.Block", "Retour de flamme : un coup bloque (burst)");
}
