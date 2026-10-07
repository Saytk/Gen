#pragma once

#include "CoreMinimal.h"
#include "ActiveGameplayEffectHandle.h"
#include "AbilitySystem/Abilities/GenGameplayAbility.h"
#include "GenGA_Projectile.generated.h"

class AGenProjectile;
class UAnimMontage;
class UGameplayEffect;
class UNiagaraSystem;

/**
 * Sort de projectile tiré vers le curseur (ex: boule de feu).
 *
 * Déroulé :
 *  1. CommitAbility (vérifie/applique le cooldown, et un coût si un CostGameplayEffect est défini)
 *  2. Si CastTime > 0 : incantation (barre de cast, ralenti), annulée si le lanceur est étourdi ou meurt
 *  3. Le client récupère le point visé sous la souris et l'envoie au serveur (target data)
 *     => on vise à la FIN de l'incantation, pas au début
 *     (ChargeMontage, optionnel, est joué pendant toute l'incantation)
 *  4. Le personnage se tourne vers la cible, joue un montage optionnel
 *  5. Le serveur fait apparaître le projectile répliqué, porteur du GE de dégâts
 */
UCLASS()
class GEN_API UGenGA_Projectile : public UGenGameplayAbility
{
	GENERATED_BODY()

public:
	UGenGA_Projectile();

protected:
	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;

	/** Fin de l'incantation (ou tout de suite si CastTime = 0) : on récupère la visée. */
	UFUNCTION()
	void OnCastFinished();

	/** Étourdi / mort pendant l'incantation. */
	UFUNCTION()
	void OnCastInterrupted();

	UFUNCTION()
	void OnTargetDataReady(const FGameplayAbilityTargetDataHandle& DataHandle);

	/** Serveur uniquement : fait apparaître le projectile en direction de TargetLocation. */
	UFUNCTION(BlueprintCallable, Category = "Gen|Projectile")
	void SpawnProjectile(const FVector& TargetLocation);

	UPROPERTY(EditDefaultsOnly, Category = "Projectile")
	TSubclassOf<AGenProjectile> ProjectileClass;

	/** GE appliqué à la cible touchée. Par défaut : UGenGE_Damage (SetByCaller.Damage). */
	UPROPERTY(EditDefaultsOnly, Category = "Projectile")
	TSubclassOf<UGameplayEffect> DamageEffectClass;

	/** Dégâts infligés (peut varier selon le niveau du sort). */
	UPROPERTY(EditDefaultsOnly, Category = "Projectile")
	FScalableFloat Damage;

	/** Distance devant le lanceur où apparaît le projectile. */
	UPROPERTY(EditDefaultsOnly, Category = "Projectile")
	float SpawnForwardOffset = 70.f;

	/** Durée d'incantation en secondes. 0 = sort instantané. */
	UPROPERTY(EditDefaultsOnly, Category = "Projectile|Cast", meta = (ClampMin = "0.0", Units = "s"))
	float CastTime = 0.f;

	/** Vitesse de déplacement pendant l'incantation (1 = pas de ralenti, 0 = immobile). */
	UPROPERTY(EditDefaultsOnly, Category = "Projectile|Cast", meta = (ClampMin = "0.0", ClampMax = "1.0", EditCondition = "CastTime > 0"))
	float CastMoveSpeedMultiplier = 0.5f;

	/** Effet sur le lanceur pendant l'incantation (vu par tous) : annonce le sort et sa direction. */
	UPROPERTY(EditDefaultsOnly, Category = "Projectile|Cast", meta = (EditCondition = "CastTime > 0"))
	TObjectPtr<UNiagaraSystem> CastFX;

	/** Socket du mesh où attacher CastFX. */
	UPROPERTY(EditDefaultsOnly, Category = "Projectile|Cast", meta = (EditCondition = "CastTime > 0"))
	FName CastFXSocket = TEXT("hand_r");

	/**
	 * Montage d'incantation (optionnel, répliqué par le GAS) : joué dès le début de l'incantation,
	 * préparation puis geste de lancer. Le régler pour que le lancer tombe à CastTime.
	 * Coupé si l'incantation est interrompue. Ignoré si CastTime = 0 (utiliser CastMontage).
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Projectile|Animation", meta = (EditCondition = "CastTime > 0"))
	TObjectPtr<UAnimMontage> ChargeMontage;

	/** Montage de lancer (optionnel, répliqué aux autres joueurs par le GAS). Joué à la fin de l'incantation. */
	UPROPERTY(EditDefaultsOnly, Category = "Projectile|Animation")
	TObjectPtr<UAnimMontage> CastMontage;

private:
	void StartCasting();

	/** Retire le ralenti et efface la barre de cast. Sans effet si on n'incante pas. */
	void StopCasting();

	FActiveGameplayEffectHandle CastSlowHandle;
	bool bIsCasting = false;
};
