#pragma once

#include "CoreMinimal.h"
#include "AbilitySystemComponent.h"
#include "GenAbilitySystemComponent.generated.h"

class UGenGameplayAbility;
class UGameplayEffect;

/**
 * ASC du projet.
 *
 * Gère l'activation des sorts par "InputTag" (pattern Lyra) : chaque sort déclare
 * son InputTag (ex: InputTag.Ability.Primary), le PlayerController transmet les
 * appuis/relâchements de touches sous forme de tags, et l'ASC active le bon sort.
 * => Changer un sort de touche = changer un tag, aucun code.
 */
UCLASS(ClassGroup = AbilitySystem, meta = (BlueprintSpawnableComponent))
class GEN_API UGenAbilitySystemComponent : public UAbilitySystemComponent
{
	GENERATED_BODY()

public:
	UGenAbilitySystemComponent();

	/** Serveur : donne les sorts et retourne leurs handles (pour pouvoir les retirer). */
	TArray<FGameplayAbilitySpecHandle> GrantAbilities(const TArray<TSubclassOf<UGenGameplayAbility>>& Abilities, UObject* SourceObject);

	/** Serveur : applique des effets sur soi-même et retourne les handles des effets actifs (durée/infinis). */
	TArray<FActiveGameplayEffectHandle> ApplyEffectsToSelf(const TArray<TSubclassOf<UGameplayEffect>>& Effects, UObject* SourceObject);

	/** Appelés par le PlayerController local. */
	void AbilityInputTagPressed(const FGameplayTag& InputTag);
	void AbilityInputTagReleased(const FGameplayTag& InputTag);

	/** Traite les inputs accumulés pendant la frame (appelé dans PlayerController::PostProcessInput). */
	void ProcessAbilityInput(float DeltaTime, bool bGamePaused);
	void ClearAbilityInput();

	/** Vrai si un autre sort actif (que Except) porte State.Casting dans ses ActivationOwnedTags. */
	bool IsAnotherAbilityCasting(FGameplayAbilitySpecHandle Except) const;

	/**
	 * Serveur : applique un contrôle dur (StateTag = un tag de GenGameplayTags::GetHardCCTags()) pendant Duration secondes.
	 * Étourdi ou neutralisé : vitesse à 0. Tous : sorts bloqués (UGenGameplayAbility::CanActivateAbility) et incantation
	 * interrompue. Point d'entrée unique de tous les contrôles durs (immunités et résilience s'y branchent).
	 * Handle invalide si rien n'est appliqué.
	 */
	UFUNCTION(BlueprintCallable, Category = "Gen|CrowdControl")
	FActiveGameplayEffectHandle ApplyHardCC(FGameplayTag StateTag, float Duration, AActor* Source);

	/** Serveur : retire les états temporaires (UGenGE_TimedState et dérivés), ex. à la mort. */
	void RemoveTimedStates();

	/**
	 * Serveur : un verrou de lancement (State.CastLocked) vient d'être posé (ex : bond en vol). MinLockDuration =
	 * sa durée la PLUS COURTE possible (bond : son atterrissage le plus précoce, pas sa durée nominale).
	 * Les activations d'un client distant ne sont refusées que pendant MinLockDuration - CastTimeTolerance
	 * (GenFeeding::GetCastLockEnforcedUntil) : le verrou du serveur finit ~½ RTT après celui du client.
	 * Toujours appelé avec le tag (UGenGA_Cast::SetCastLock) : sans lui la fenêtre reste fermée et le serveur ne
	 * refuse rien (choix sûr : un client honnête n'est jamais refusé ; seul le côté qui prédit fait respecter le verrou).
	 */
	void NoteCastLock(float MinLockDuration);

	/** Serveur : fin (temps du monde) de la fenêtre où le verrou refuse les activations d'un client distant. */
	double GetCastLockEnforcedUntil() const { return CastLockEnforcedUntil; }

	/** Toutes les machines : retire State.CastLocked et ferme la fenêtre (mort : un verrou oublié bloquerait tout après le respawn). */
	void ClearCastLock();

protected:
	virtual void AbilitySpecInputPressed(FGameplayAbilitySpec& Spec) override;
	virtual void AbilitySpecInputReleased(FGameplayAbilitySpec& Spec) override;

	TArray<FGameplayAbilitySpecHandle> InputPressedSpecHandles;
	TArray<FGameplayAbilitySpecHandle> InputReleasedSpecHandles;
	TArray<FGameplayAbilitySpecHandle> InputHeldSpecHandles;

	/** Serveur : fin de la fenêtre où State.CastLocked refuse les activations d'un client distant. */
	double CastLockEnforcedUntil = -1.0;
};
