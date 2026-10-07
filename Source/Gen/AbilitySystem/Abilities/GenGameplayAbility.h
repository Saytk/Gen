#pragma once

#include "CoreMinimal.h"
#include "Abilities/GameplayAbility.h"
#include "ScalableFloat.h"
#include "GenGameplayAbility.generated.h"

class AGenCharacterBase;
class AGenPlayerController;

UENUM(BlueprintType)
enum class EGenAbilityActivationPolicy : uint8
{
	/** S'active une fois à l'appui. */
	OnInputTriggered,

	/** Se relance en boucle tant que la touche est maintenue (ex: auto-attaque M1). */
	WhileInputActive
};

/**
 * Classe de base de tous les sorts du jeu.
 *
 * - Instanciée par acteur, prédite localement (réactivité côté client, validée par le serveur).
 * - Cooldown générique : renseignez juste CooldownDuration + CooldownTags dans le Blueprint
 *   (laissez CooldownGameplayEffectClass vide), le GE partagé UGenGE_Cooldown reçoit
 *   la durée et le tag dynamiquement. Un GE de cooldown personnalisé reste possible.
 * - Bloquée automatiquement si le lanceur est mort ou étourdi.
 */
UCLASS(Abstract)
class GEN_API UGenGameplayAbility : public UGameplayAbility
{
	GENERATED_BODY()

public:
	UGenGameplayAbility();

	/** Tags posés sur le lanceur tant que le sort est actif (accès en lecture, ActivationOwnedTags est protégé). */
	const FGameplayTagContainer& GetActivationOwnedTagsRO() const { return ActivationOwnedTags; }

	/** Touche qui déclenche ce sort (InputTag.Ability.*). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gen|Input", meta = (Categories = "InputTag"))
	FGameplayTag InputTag;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gen|Input")
	EGenAbilityActivationPolicy ActivationPolicy = EGenAbilityActivationPolicy::OnInputTriggered;

	/** Nom affiché dans l'UI. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gen|UI")
	FText DisplayName;

	/** Durée du cooldown en secondes (peut varier selon le niveau du sort). 0 = pas de cooldown. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Cooldowns")
	FScalableFloat CooldownDuration;

	/** Tags accordés pendant le cooldown (ex: Cooldown.Ability.Fireball). Doit être unique par sort. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Cooldowns", meta = (Categories = "Cooldown"))
	FGameplayTagContainer CooldownTags;

	//~ UGameplayAbility
	virtual const FGameplayTagContainer* GetCooldownTags() const override;
	virtual void ApplyCooldown(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo) const override;

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
#endif

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Gen|Ability")
	AGenCharacterBase* GetGenCharacterFromActorInfo() const;

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Gen|Ability")
	AGenPlayerController* GetGenPlayerControllerFromActorInfo() const;

private:
	/** Conteneur temporaire renvoyé par GetCooldownTags() (tags du GE + CooldownTags). */
	mutable FGameplayTagContainer TempCooldownTags;
};
