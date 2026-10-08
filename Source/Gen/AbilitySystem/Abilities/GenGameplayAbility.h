#pragma once

#include "CoreMinimal.h"
#include "Abilities/GameplayAbility.h"
#include "ScalableFloat.h"
#include "GenGameplayAbility.generated.h"

class AGenCharacterBase;
class AGenPlayerController;
class UTexture2D;
struct FGenAimGeometry;

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

	/** Activation en cours déclenchée par un événement ou un tag (AbilityTriggers), pas par la touche du joueur. */
	bool IsTriggeredActivation() const { return CurrentEventData.EventTag.IsValid(); }

	/** Touche qui déclenche ce sort (InputTag.Ability.*). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gen|Input", meta = (Categories = "InputTag"))
	FGameplayTag InputTag;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gen|Input")
	EGenAbilityActivationPolicy ActivationPolicy = EGenAbilityActivationPolicy::OnInputTriggered;

	/** Nom affiché dans l'UI. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gen|UI")
	FText DisplayName;

	/** Icône de l'emplacement dans la barre de sorts (UI_Guidelines §2.11 : 256 px, affichée à 64 / 72 px). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gen|UI")
	TSoftObjectPtr<UTexture2D> Icon;

	/** Durée du cooldown en secondes (peut varier selon le niveau du sort). 0 = pas de cooldown. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Cooldowns")
	FScalableFloat CooldownDuration;

	/** Tags accordés pendant le cooldown (ex: Cooldown.Ability.Fireball). Doit être unique par sort. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Cooldowns", meta = (Categories = "Cooldown"))
	FGameplayTagContainer CooldownTags;

	/**
	 * Énergie dépensée au lancer (R : 25, F : 100), lue aussi par la barre de sorts. 0 = gratuit.
	 * Vérifiée à l'activation (CheckCost), payée par CommitAbility (ApplyCost) : au lancer pour UGenGA_Cast,
	 * donc une incantation annulée ou interrompue ne coûte rien (guidelines §3.1). Python : energy_cost.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gen|Cost", meta = (ClampMin = "0.0"))
	float EnergyCost = 0.f;

	//~ UGameplayAbility
	virtual const FGameplayTagContainer* GetCooldownTags() const override;
	virtual void ApplyCooldown(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo) const override;

	/**
	 * Refuse aussi sous un contrôle dur (GenGameplayTags::GetHardCCTags) et pendant State.CastLocked (bond en vol,
	 * forme de feu...), posé par code et non par les assets. Le verrou fait foi du côté qui prédit ; le serveur ne
	 * l'applique à un client distant que dans sa fenêtre (UGenAbilitySystemComponent::GetCastLockEnforcedUntil).
	 */
	virtual bool CanActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayTagContainer* SourceTags = nullptr, const FGameplayTagContainer* TargetTags = nullptr, FGameplayTagContainer* OptionalRelevantTags = nullptr) const override;

	/** Refuse aussi sous EnergyCost (GenEnergy::CanAfford, même règle que la barre de sorts). */
	virtual bool CheckCost(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, FGameplayTagContainer* OptionalRelevantTags = nullptr) const override;
	/** Dépense EnergyCost (UGenGE_Gain négatif), dans la fenêtre de prédiction du commit. */
	virtual void ApplyCost(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo) const override;

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
#endif

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Gen|Ability")
	AGenCharacterBase* GetGenCharacterFromActorInfo() const;

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Gen|Ability")
	AGenPlayerController* GetGenPlayerControllerFromActorInfo() const;

	/**
	 * Indicateur de visée du lanceur (local seulement) : géométrie calculée depuis les valeurs de jeu de CE sort.
	 * Faux = aucun indicateur. Appelé chaque image tant que la visée est ouverte (UGenSpellIndicatorComponent).
	 */
	virtual bool GetAimGeometry(const AGenCharacterBase& Caster, int32 Fed, const FVector& Cursor, FGenAimGeometry& Out) const { return false; }

	/**
	 * Télégraphe centré sur le lanceur, vu par tous pendant l'incantation (bChannel faux) ou la canalisation (vrai).
	 * Rayon en cm, 0 = aucun. Combustion : NovaRadius pendant l'incantation ; Living Flame : BurstRadius pendant la forme.
	 */
	virtual float GetSelfTelegraphRadius(bool bChannel) const { return 0.f; }

private:
	/** Conteneur temporaire renvoyé par GetCooldownTags() (tags du GE + CooldownTags). */
	mutable FGameplayTagContainer TempCooldownTags;
};
