#pragma once

#include "NativeGameplayTags.h"

/**
 * Tags natifs du jeu. Ils sont enregistrés automatiquement au chargement du module
 * et apparaissent dans l'éditeur (Project Settings > GameplayTags) sans config .ini.
 */
namespace GenGameplayTags
{
	// --- Input : un tag par slot de sort (style Battlerite) ---
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(InputTag_Ability_Primary);   // Clic gauche (M1)
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(InputTag_Ability_Secondary); // Clic droit (M2)
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(InputTag_Ability_Mobility);  // Espace
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(InputTag_Ability_1);         // A (AZERTY)
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(InputTag_Ability_2);         // E
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(InputTag_Ability_3);         // R
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(InputTag_Ability_Ultimate);  // F

	// --- Abilities ---
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Ability_Fireball);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Ability_GreatFireball);

	// --- Cooldowns ---
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Cooldown_Ability_Fireball);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Cooldown_Ability_GreatFireball);

	// --- États ---
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Dead);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Stunned);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Casting);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Countering);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_CastLocked);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Untouchable);

	// Contrôles durs (guidelines §3.2) en plus de State_Stunned. Pas encore appliqués par un sort,
	// mais les interruptions, les blocages de sorts et la barre de sorts les traitent déjà.
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Silenced);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Feared);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Incapacitated);

	// --- Événements (gameplay events) ---
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_Counter_Blocked);

	// --- SetByCaller (magnitudes passées par le code aux GameplayEffects) ---
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(SetByCaller_Damage);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(SetByCaller_Cooldown);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(SetByCaller_MoveSpeedMultiplier);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(SetByCaller_Energy);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(SetByCaller_Resource);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(SetByCaller_Duration);

	/**
	 * Contrôles durs (guidelines §3.2) : étourdi, silence, peur, neutralisé. Ils interrompent les incantations
	 * (UGenGA_Cast) et bloquent l'activation des sorts (UGenGameplayAbility::CanActivateAbility).
	 */
	const FGameplayTagContainer& GetHardCCTags();
}
