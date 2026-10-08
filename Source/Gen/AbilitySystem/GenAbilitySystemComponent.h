#pragma once

#include "CoreMinimal.h"
#include "AbilitySystemComponent.h"
#include "GenAbilitySystemComponent.generated.h"

class UGenGameplayAbility;
class UGameplayEffect;

/** Sort accordé (bRemoved = faux) ou retiré (vrai), sur le serveur et sur le client propriétaire (réplication des specs). */
DECLARE_MULTICAST_DELEGATE_TwoParams(FGenOnAbilitiesChanged, const FGameplayAbilitySpec& /*Spec*/, bool /*bRemoved*/);

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
	 * Sorts accordés ou retirés (respawn, changement de champion) : l'interface se recâble dessus au lieu d'interroger
	 * l'ASC. Retrait : diffusé AVANT que le spec quitte la liste des sorts activables.
	 */
	FGenOnAbilitiesChanged OnAbilitiesChanged;

protected:
	virtual void OnGiveAbility(FGameplayAbilitySpec& AbilitySpec) override;
	virtual void OnRemoveAbility(FGameplayAbilitySpec& AbilitySpec) override;

	virtual void AbilitySpecInputPressed(FGameplayAbilitySpec& Spec) override;
	virtual void AbilitySpecInputReleased(FGameplayAbilitySpec& Spec) override;

	TArray<FGameplayAbilitySpecHandle> InputPressedSpecHandles;
	TArray<FGameplayAbilitySpecHandle> InputReleasedSpecHandles;
	TArray<FGameplayAbilitySpecHandle> InputHeldSpecHandles;
};
