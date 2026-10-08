#pragma once

#include "CoreMinimal.h"
#include "ActiveGameplayEffectHandle.h"
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
 *     Une seule barre de cast de l'appui au lancer : un cran par flamme, repliée à la fin du nourrissage.
 *  1. Si CastTime > 0 : incantation (barre de cast, ralenti), annulée par un contrôle dur (GenGameplayTags::GetHardCCTags) ou la mort
 *     (ou par un autre sort via CancelAbilitiesWithTag)
 *  2. Le client récupère le point visé sous la souris et l'envoie au serveur (target data,
 *     avec le nombre d'unités nourries) => on vise à la FIN de l'incantation.
 *     Serveur pour un client distant : pas de minuteur propre, c'est l'arrivée de la visée qui
 *     termine l'incantation (durée vérifiée à CastTimeTolerance près, voir GenFeeding::GetServerCastWait).
 *     Visée arrivée trop tôt : le serveur garde toute l'incantation jusqu'à sa propre fin (barre et effet vus
 *     par les autres, ralenti, interruption par un contrôle dur), puis lance le tir (coût, cooldown) et le
 *     projectile. Une seule visée par activation ; les autres sorts du joueur ne peuvent plus annuler ce tir.
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

	/** Serveur : compte exact annoncé par le client distant à la fin de son nourrissage (affichage seulement, borné par le temps). */
	void ApplyReportedFedCount(int32 Reported);

	//~ UGameplayAbility
	/** Faux sur le serveur pendant l'attente d'un projectile différé : le tir est parti côté client. */
	virtual bool CanBeCanceled() const override;

	/** Serveur : visée reçue en avance, le tir attend la fin de l'incantation mesurée par le serveur (tests). */
	bool IsWaitingForDeferredLaunch() const { return bServerShotLocked && PendingAimData.Num() > 0; }

protected:
	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;

	/** Fin de l'incantation (ou tout de suite si CastTime = 0) : on récupère la visée. */
	UFUNCTION()
	void OnCastFinished();

	/** Contrôle dur (étourdi, silence, peur, neutralisé) pendant le nourrissage ou l'incantation. */
	UFUNCTION()
	void OnCastInterrupted();

	UFUNCTION()
	void OnTargetDataReady(const FGameplayAbilityTargetDataHandle& DataHandle);

	/** Serveur pour un client distant : visée reçue pendant l'incantation. */
	UFUNCTION()
	void OnServerAimReceived(const FGameplayAbilityTargetDataHandle& DataHandle);

	/** Serveur : visée arrivée trop tôt, fin de l'incantation du serveur => le tir (coût, cooldown) et le projectile partent. */
	UFUNCTION()
	void OnServerLaunchDelayFinished();

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

	/** Une unité absorbée toutes les FeedInterval secondes. Par défaut : CurffeTuning::FeedInterval (0.3 s). */
	UPROPERTY(EditDefaultsOnly, Category = "Projectile|Feeding", meta = (EditCondition = "bFeedable", ClampMin = "0.05", Units = "s"))
	float FeedInterval;

	/**
	 * Seuils de nourrissage du sort (crans de la barre), même si le champion a plus de ressource.
	 * Par défaut : CurffeTuning::MaxFeedPerSpell (3).
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Projectile|Feeding", meta = (EditCondition = "bFeedable", ClampMin = "1"))
	int32 MaxFeed;

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
	/**
	 * Serveur pour un client distant : recalcule l'affichage (estimation ou annonce du client bornée par le temps,
	 * jamais en recul, GenFeeding::ReconcileDisplayedFed), l'applique et le renvoie.
	 */
	int32 ReconcileFedVisual();
	/** Barre de cast : fin du nourrissage avec Count flammes (rappelable pour corriger le compte, sans recul sauf bFinal). */
	void MarkFeedEnded(int32 Count, bool bFinal = false);
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
	/**
	 * Lancer : borne le nourrissage, CommitAbility (cooldown, coût), dépense les flammes, tourne le lanceur,
	 * joue le montage. Faux si le sort a été terminé (visée invalide, commit refusé).
	 */
	bool ReleaseShot(const FGameplayAbilityTargetDataHandle& DataHandle, FVector& OutDirection, int32& OutFed);
	/** Fait apparaître le projectile (serveur) et termine le sort. */
	void LaunchShot(const FVector& Direction, int32 Fed);

	/** Nombre d'unités nourries retenu pour ce tir (borné côté serveur). bLog = faux : sans journal (affichage). */
	int32 ResolveFedCount(const FGameplayAbilityTargetData* Data, bool bLog = true) const;

	/** Serveur : journal d'une visée en avance (Warning limité si elle est très en avance, GenFeeding::IsAimSuspiciouslyEarly). */
	void LogEarlyAim(float Elapsed, float Wait) const;

	/** Serveur : acquitte la clé de prédiction d'une visée différée (le client retire ses valeurs prédites). */
	void AcknowledgeDeferredAim();
	void SpendResource(int32 Amount);

	FActiveGameplayEffectHandle CastSlowHandle;

	UPROPERTY(Transient)
	TObjectPtr<UAbilityTask_WaitDelay> FeedTickTask;

	UPROPERTY(Transient)
	TObjectPtr<UAbilityTask_WaitInputRelease> FeedReleaseTask;

	int32 FedCount = 0;
	int32 FedVisualCount = 0;
	/** Flammes disponibles à l'appui (crans de la barre) : plafond du nourrissage. */
	int32 FeedSlotsAtPress = 0;
	bool bIsFeeding = false;
	bool bInterruptWatchStarted = false;
	float FeedStartTime = 0.f;
	/** Serveur : durée du nourrissage mesurée entre l'activation et le signal du client. */
	float ServerFeedElapsed = 0.f;
	/** Serveur : compte brut annoncé par le client (ServerReportFedResource), sinon INDEX_NONE. */
	int32 ReportedFedCount = INDEX_NONE;

	/** Début de l'incantation (après le nourrissage), en temps du monde. */
	float CastStartTime = 0.f;
	/** Serveur : visée du client reçue, le tir est parti (le sort n'est plus annulable par un autre sort). */
	bool bServerShotLocked = false;
	/** Serveur : visée reçue en avance, traitée (coût, cooldown, projectile) à la fin de l'incantation du serveur. */
	FGameplayAbilityTargetDataHandle PendingAimData;
	/** Serveur : clé de prédiction de la visée différée, acquittée au départ du tir ou à son annulation. */
	FPredictionKey DeferredAimKey;
};
