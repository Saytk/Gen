#include "GenGameplayTags.h"

namespace GenGameplayTags
{
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(InputTag_Ability_Primary, "InputTag.Ability.Primary", "Slot M1 (clic gauche)");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(InputTag_Ability_Secondary, "InputTag.Ability.Secondary", "Slot M2 (clic droit)");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(InputTag_Ability_Mobility, "InputTag.Ability.Mobility", "Slot mobilite (Espace)");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(InputTag_Ability_1, "InputTag.Ability.1", "Slot 1 (A en AZERTY)");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(InputTag_Ability_2, "InputTag.Ability.2", "Slot 2 (E)");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(InputTag_Ability_3, "InputTag.Ability.3", "Slot 3 (R)");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(InputTag_Ability_Ultimate, "InputTag.Ability.Ultimate", "Slot ultime (F)");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Ability_Fireball, "Ability.Fireball", "Sort : boule de feu");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Ability_GreatFireball, "Ability.GreatFireball", "Sort : grosse boule de feu (incantation longue)");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Cooldown_Ability_Fireball, "Cooldown.Ability.Fireball", "Recharge de la boule de feu");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Cooldown_Ability_GreatFireball, "Cooldown.Ability.GreatFireball", "Recharge de la grosse boule de feu");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(State_Dead, "State.Dead", "Le personnage est mort");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(State_Stunned, "State.Stunned", "Le personnage est etourdi (bloque les sorts)");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(State_Casting, "State.Casting", "Le personnage est en train de lancer un sort");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(State_Countering, "State.Countering", "Posture de contre : projectiles et melee sont bloques, pas les zones au sol");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(State_CastLocked, "State.CastLocked", "Ne peut lancer aucun sort (bond en vol, forme de feu...). Pose uniquement par UGenGA_Cast::SetCastLock");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(State_Untouchable, "State.Untouchable", "Intouchable : projectiles traversent, degats et controles ignores (<= 0.5 s, guidelines 3.5)");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(State_Silenced, "State.Silenced", "Controle dur : peut bouger, ne peut pas lancer de sort");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(State_Feared, "State.Feared", "Controle dur : fuit la source, ne peut pas lancer de sort");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(State_Incapacitated, "State.Incapacitated", "Controle dur : comme un etourdissement, prend fin au moindre degat");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Event_Counter_Blocked, "Event.Counter.Blocked", "Un coup a ete bloque par le contre de la cible (serveur)");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(SetByCaller_Damage, "SetByCaller.Damage", "Degats passes au GE de degats");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(SetByCaller_Cooldown, "SetByCaller.Cooldown", "Duree passee au GE de cooldown");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(SetByCaller_MoveSpeedMultiplier, "SetByCaller.MoveSpeedMultiplier", "Multiplicateur de vitesse (ex: 0.5 = ralenti de moitie)");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(SetByCaller_Energy, "SetByCaller.Energy", "Energie gagnee (negatif = depensee)");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(SetByCaller_Resource, "SetByCaller.Resource", "Ressource du champion gagnee (negatif = depensee), ex : flammes de Curffe");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(SetByCaller_Duration, "SetByCaller.Duration", "Duree passee aux etats temporaires (contre, etourdissement...)");

	const FGameplayTagContainer& GetHardCCTags()
	{
		// Construit au premier appel (les tags natifs sont alors enregistrés)
		static const FGameplayTagContainer HardCCTags = []()
		{
			FGameplayTagContainer Tags;
			Tags.AddTag(State_Stunned);
			Tags.AddTag(State_Silenced);
			Tags.AddTag(State_Feared);
			Tags.AddTag(State_Incapacitated);
			return Tags;
		}();
		return HardCCTags;
	}
}
