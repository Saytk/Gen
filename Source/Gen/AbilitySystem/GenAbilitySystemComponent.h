#pragma once

#include "CoreMinimal.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystem/GenResilience.h"
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
	 * Serveur : applique un contrôle dur (StateTag = un tag de GenGameplayTags::GetHardCCTags()) pendant Duration secondes.
	 * Étourdi ou neutralisé : vitesse à 0. Tous : sorts bloqués (UGenGameplayAbility::CanActivateAbility) et incantation
	 * interrompue. Point d'entrée unique de tous les contrôles durs (immunités et résilience s'y branchent).
	 * Ignoré sous State.Untouchable et State.CCImmune. Résilience (guidelines §3.3) : 2.5 s de contrôle dur sur 5 s
	 * => State.CCImmune jusqu'à 1.5 s après la fin du contrôle qui atteint le seuil (lui s'applique en entier).
	 * Handle invalide si rien n'est appliqué.
	 */
	UFUNCTION(BlueprintCallable, Category = "Gen|CrowdControl")
	FActiveGameplayEffectHandle ApplyHardCC(FGameplayTag StateTag, float Duration, AActor* Source);

	/** Serveur : retire les états temporaires (UGenGE_TimedState et dérivés) et oublie l'historique de résilience, ex. à la mort. */
	void RemoveTimedStates();

	/**
	 * Touche d'annulation (Plan 3 Task 6, guidelines §3.1) : annule les sorts encore en nourrissage ou en incantation
	 * (UGenGA_Cast::IsCastPending), annulation prédite et répliquée au serveur. Un sort déjà parti (fenêtre de contre,
	 * bond en vol, forme de feu) n'est jamais annulé. Serveur, visée du client déjà reçue : CanBeCanceled est faux, le
	 * sort part et ses coûts restent payés. Un sort annulé dont la touche reste enfoncée ne se relance pas (répétition
	 * automatique) avant un nouvel appui. Renvoie le nombre de sorts annulés.
	 */
	int32 CancelPendingCasts();

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

	/**
	 * Revue V2-V4, I1 : le tag Tag (State.FastFeeding ou State.FreeResource seulement) a-t-il été retiré (bAdded faux) ou
	 * posé (vrai) dans la fenêtre de grâce autour de ReferenceTime (temps du monde) ? Voir GenFeeding::ServerTagGrace.
	 */
	bool WasGraceTagChangedNear(const FGameplayTag& Tag, bool bAdded, double ReferenceTime) const;

	/** Toutes les machines : retire State.CastLocked et ferme la fenêtre (mort : un verrou oublié bloquerait tout après le respawn). */
	void ClearCastLock();

	/**
	 * Serveur : clé de prédiction portée par la dernière visée (target data) reçue du client pour ce sort et cette
	 * activation, sinon une clé invalide. Sert à reporter son acquittement (UGenGA_Cast, visée en avance).
	 */
	FPredictionKey GetReplicatedTargetDataKey(FGameplayAbilitySpecHandle Handle, FPredictionKey ActivationKey) const;

	/**
	 * Sorts accordés ou retirés (respawn, changement de champion) : l'interface se recâble dessus au lieu d'interroger
	 * l'ASC. Retrait : diffusé AVANT que le spec quitte la liste des sorts activables.
	 */
	FGenOnAbilitiesChanged OnAbilitiesChanged;

protected:
	/** Retient l'heure des changements de State.FastFeeding et State.FreeResource (WasGraceTagChangedNear). */
	virtual void OnTagUpdated(const FGameplayTag& Tag, bool TagExists) override;

	virtual void OnGiveAbility(FGameplayAbilitySpec& AbilitySpec) override;
	/** Un sort retiré efface l'affichage des unités nourries qu'il possédait (AGenCharacterBase::ClearFedResourceFrom). */
	virtual void OnRemoveAbility(FGameplayAbilitySpec& AbilitySpec) override;

	virtual void AbilitySpecInputPressed(FGameplayAbilitySpec& Spec) override;
	virtual void AbilitySpecInputReleased(FGameplayAbilitySpec& Spec) override;

	TArray<FGameplayAbilitySpecHandle> InputPressedSpecHandles;
	TArray<FGameplayAbilitySpecHandle> InputReleasedSpecHandles;
	TArray<FGameplayAbilitySpecHandle> InputHeldSpecHandles;

	/** Serveur : fin de la fenêtre où State.CastLocked refuse les activations d'un client distant. */
	double CastLockEnforcedUntil = -1.0;

	/** Serveur : contrôles durs reçus récemment (résilience, Plan 3 Task 5). */
	GenResilience::FHardCCHistory HardCCHistory;

	/** Revue V2-V4, I1 : derniers ajout et retrait (temps du monde) d'un tag à fenêtre de grâce. */
	struct FGraceTagTimes
	{
		double Added = -1.e9;
		double Removed = -1.e9;
	};
	TMap<FGameplayTag, FGraceTagTimes> GraceTagTimes;
};
