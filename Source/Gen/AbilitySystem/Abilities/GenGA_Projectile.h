#pragma once

#include "CoreMinimal.h"
#include "ActiveGameplayEffectHandle.h"
#include "GameplayPrediction.h"
#include "Abilities/GameplayAbilityTargetTypes.h"
#include "AbilitySystem/Abilities/GenGameplayAbility.h"
#include "GenGA_Projectile.generated.h"

class AGenProjectile;
class UAbilityTask_WaitDelay;
class UAbilityTask_WaitInputRelease;
class UAnimMontage;
class UGameplayEffect;
class UNiagaraSystem;
struct FGameplayAbilityTargetData;

/**
 * Sort de projectile tiré vers le curseur (ex: boule de feu).
 *
 * Déroulé :
 *  0. Si bFeedable : nourrissage. Tant que la touche est maintenue, une unité de ressource
 *     (flammes de Curffe) passe dans le sort toutes les FeedInterval s, jusqu'à MaxFeed.
 *     Relâcher, atteindre le max ou épuiser la ressource enchaîne sur l'incantation.
 *     Le client décide du nombre ; le serveur le borne à sa ressource et au temps mesuré.
 *  1. Si CastTime > 0 : incantation (barre de cast, ralenti), annulée si le lanceur est étourdi ou meurt
 *     (ou par un autre sort via CancelAbilitiesWithTag)
 *  2. Le client récupère le point visé sous la souris et l'envoie au serveur (target data,
 *     avec le nombre d'unités nourries) => on vise à la FIN de l'incantation.
 *     Serveur pour un client distant : pas de minuteur propre, c'est l'arrivée de la visée qui
 *     termine l'incantation (durée vérifiée à CastTimeTolerance près). Dès qu'elle est arrivée,
 *     les autres sorts du joueur ne peuvent plus annuler ce tir (le client l'a déjà lancé).
 *  3. CommitAbility + dépense de la ressource nourrie au lancer : une incantation interrompue ne coûte rien.
 *  4. Le personnage se tourne vers la cible, joue un montage optionnel
 *  5. Le serveur fait apparaître le projectile répliqué, mis à l'échelle par le nourrissage
 *     (dégâts, vitesse, taille, explosion de zone, repoussement), porteur du GE de dégâts
 *     et des gains du lanceur (énergie, ressource) s'il touche.
 */
UCLASS()
class GEN_API UGenGA_Projectile : public UGenGameplayAbility
{
	GENERATED_BODY()

public:
	UGenGA_Projectile();

	/** Serveur : compte exact annoncé par le client distant à la fin de son nourrissage (affichage seulement). */
	void ApplyReportedFedCount(int32 Reported);

	//~ UGameplayAbility
	/** Faux sur le serveur une fois la visée du client reçue : le tir est parti côté client. */
	virtual bool CanBeCanceled() const override;

protected:
	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;

	/** Fin de l'incantation (ou tout de suite si CastTime = 0) : on récupère la visée. */
	UFUNCTION()
	void OnCastFinished();

	/** Étourdi pendant le nourrissage ou l'incantation. */
	UFUNCTION()
	void OnCastInterrupted();

	UFUNCTION()
	void OnTargetDataReady(const FGameplayAbilityTargetDataHandle& DataHandle);

	/** Serveur pour un client distant : visée reçue pendant l'incantation. */
	UFUNCTION()
	void OnServerAimReceived(const FGameplayAbilityTargetDataHandle& DataHandle);

	/** Serveur : visée arrivée trop tôt, fin de l'attente du reste de l'incantation. */
	UFUNCTION()
	void OnServerCastWaitFinished();

	UFUNCTION()
	void OnFeedTick();

	UFUNCTION()
	void OnFeedInputReleased(float TimeHeld);

	/** Fin du nourrissage, synchronisée client -> serveur. */
	UFUNCTION()
	void OnFeedSynced();

	/** Serveur uniquement : fait apparaître le projectile en direction de TargetLocation. */
	UFUNCTION(BlueprintCallable, Category = "Gen|Projectile")
	void SpawnProjectile(const FVector& TargetLocation, int32 Fed = 0);

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

	/** Durée d'incantation en secondes (après le nourrissage). 0 = sort instantané. */
	UPROPERTY(EditDefaultsOnly, Category = "Projectile|Cast", meta = (ClampMin = "0.0", Units = "s"))
	float CastTime = 0.f;

	/** Vitesse de déplacement pendant le nourrissage et l'incantation (1 = pas de ralenti, 0 = immobile). */
	UPROPERTY(EditDefaultsOnly, Category = "Projectile|Cast", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float CastMoveSpeedMultiplier = 0.5f;

	/** Effet sur le lanceur pendant l'incantation (vu par tous) : annonce le sort et sa direction. */
	UPROPERTY(EditDefaultsOnly, Category = "Projectile|Cast")
	TObjectPtr<UNiagaraSystem> CastFX;

	/** Socket du mesh où attacher CastFX. */
	UPROPERTY(EditDefaultsOnly, Category = "Projectile|Cast")
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

	/** Maintenir la touche nourrit le sort avec la ressource du champion (attribut Resource). */
	UPROPERTY(EditDefaultsOnly, Category = "Projectile|Feeding")
	bool bFeedable = false;

	/** Une unité absorbée toutes les FeedInterval secondes. */
	UPROPERTY(EditDefaultsOnly, Category = "Projectile|Feeding", meta = (EditCondition = "bFeedable", ClampMin = "0.05", Units = "s"))
	float FeedInterval = 0.2f;

	UPROPERTY(EditDefaultsOnly, Category = "Projectile|Feeding", meta = (EditCondition = "bFeedable", ClampMin = "1"))
	int32 MaxFeed = 5;

	UPROPERTY(EditDefaultsOnly, Category = "Projectile|Feeding", meta = (EditCondition = "bFeedable"))
	float DamagePerFeed = 0.f;

	UPROPERTY(EditDefaultsOnly, Category = "Projectile|Feeding", meta = (EditCondition = "bFeedable"))
	float EnergyPerFeed = 0.f;

	/** Vitesse du projectile à MaxFeed, en multiple de sa vitesse de base. */
	UPROPERTY(EditDefaultsOnly, Category = "Projectile|Feeding", meta = (EditCondition = "bFeedable", ClampMin = "0.1"))
	float SpeedMultiplierAtMaxFeed = 1.f;

	/** Taille du projectile (visuel + collision) à MaxFeed. */
	UPROPERTY(EditDefaultsOnly, Category = "Projectile|Feeding", meta = (EditCondition = "bFeedable", ClampMin = "0.1"))
	float ScaleAtMaxFeed = 1.f;

	/** Explosion de zone à partir de N unités nourries (0 = jamais). */
	UPROPERTY(EditDefaultsOnly, Category = "Projectile|Feeding", meta = (EditCondition = "bFeedable", ClampMin = "0"))
	int32 ExplosionMinFeed = 0;

	UPROPERTY(EditDefaultsOnly, Category = "Projectile|Feeding", meta = (EditCondition = "bFeedable", Units = "cm"))
	float ExplosionRadius = 150.f;

	/** Repoussement à partir de N unités nourries (0 = jamais). */
	UPROPERTY(EditDefaultsOnly, Category = "Projectile|Feeding", meta = (EditCondition = "bFeedable", ClampMin = "0"))
	int32 KnockbackMinFeed = 0;

	UPROPERTY(EditDefaultsOnly, Category = "Projectile|Feeding", meta = (EditCondition = "bFeedable", Units = "cm"))
	float KnockbackDistance = 400.f;

	/** Énergie gagnée par le lanceur si le projectile touche au moins un ennemi. */
	UPROPERTY(EditDefaultsOnly, Category = "Projectile|Gains")
	float EnergyOnHit = 0.f;

	/** Ressource (flammes...) gagnée par le lanceur si le projectile touche au moins un ennemi. */
	UPROPERTY(EditDefaultsOnly, Category = "Projectile|Gains")
	float ResourceOnHit = 0.f;

private:
	void StartFeeding();
	void ScheduleFeedTick();
	/** Client (ou hôte) : fin du nourrissage => prévient le serveur puis incante. */
	void StopFeedingLocal();
	void EndFeedTasks();
	void SetFedVisual(int32 Count);
	int32 GetAvailableFeed() const;

	void StartCasting();
	void ApplyCastSlow();
	void StartInterruptWatch();
	/** Retire le ralenti et la barre de cast (garde le visuel des unités nourries). */
	void EndCastPresentation();
	/** Nettoyage complet (fin ou annulation du sort). */
	void StopCasting();

	/** Serveur qui exécute le sort d'un client distant (ni hôte, ni autonome, ni IA). */
	bool IsServerForRemoteClient() const;
	/** Serveur : tire avec la visée reçue du client (ou annule si étourdi / mort entre-temps). */
	void FinishServerCast();

	/** Nombre d'unités nourries retenu pour ce tir (borné côté serveur). */
	int32 ResolveFedCount(const FGameplayAbilityTargetData* Data) const;
	void SpendResource(int32 Amount);

	FActiveGameplayEffectHandle CastSlowHandle;

	UPROPERTY(Transient)
	TObjectPtr<UAbilityTask_WaitDelay> FeedTickTask;

	UPROPERTY(Transient)
	TObjectPtr<UAbilityTask_WaitInputRelease> FeedReleaseTask;

	int32 FedCount = 0;
	int32 FedVisualCount = 0;
	bool bIsFeeding = false;
	bool bInterruptWatchStarted = false;
	float FeedStartTime = 0.f;
	/** Serveur : durée du nourrissage mesurée entre l'activation et le signal du client. */
	float ServerFeedElapsed = 0.f;
	/** Serveur : compte brut annoncé par le client (ServerReportFedResource), sinon INDEX_NONE. */
	int32 ReportedFedCount = INDEX_NONE;

	/** Début de l'incantation (après le nourrissage), en temps du monde. */
	float CastStartTime = 0.f;
	/** Serveur : visée du client reçue, le tir partira (le sort n'est plus annulable par un autre sort). */
	bool bServerShotLocked = false;
	/** Serveur : visée reçue en avance, gardée jusqu'à la fin de l'incantation. */
	FGameplayAbilityTargetDataHandle PendingAimData;
	/** Serveur : clé de prédiction avec laquelle la visée est arrivée (le client y a prédit coût et cooldown). */
	FPredictionKey PendingAimPredictionKey;
};
