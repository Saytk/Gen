#pragma once

#include "CoreMinimal.h"
#include "ActiveGameplayEffectHandle.h"
#include "Abilities/GameplayAbilityTargetTypes.h"
#include "AbilitySystem/Abilities/GenGameplayAbility.h"
#include "GenGA_Cast.generated.h"

class AGenGroundArea;
class AGenProjectile;
class UAbilityTask_PlayMontageAndWait;
class UAbilityTask_WaitDelay;
class UAbilityTask_WaitInputRelease;
class UGenAbilityTask_TargetDataUnderCursor;
class UAnimMontage;
class UGameplayEffect;
class UNiagaraSystem;
struct FGameplayAbilityTargetData;
struct FGenTargetData_Aim;
struct FGenAreaParams;
struct FGenProjectileSalvo;
struct FGenProjectileShotParams;

/** Ce que le sort sait quand il part : après nourrissage, incantation et visée, coûts déjà payés. */
struct FGenCastRelease
{
	/** Point visé sous le curseur (plan horizontal du lanceur). */
	FVector AimLocation = FVector::ZeroVector;
	/** Direction horizontale lanceur -> visée, jamais nulle. */
	FVector AimDirection = FVector::ForwardVector;
	/** Unités nourries, validées par le serveur et déjà dépensées. */
	int32 Fed = 0;
	/**
	 * Bond annoncé par la visée du client (FGenTargetData_Aim::LeapDistance / LeapYaw), < 0 = aucun. Le sort le valide
	 * (GenAreaRules::AcceptClientLeap) : revue Plan 2 Tasks 7-8, I-1.
	 */
	float ClientLeapDistance = -1.f;
	float ClientLeapYaw = 0.f;
};

/**
 * Sort à incantation : base de tous les sorts qui "partent" (projectile, zone au sol, bond, contre...).
 *
 * Déroulé :
 *  0. Si bFeedable : nourrissage. Tant que la touche est maintenue, une unité de ressource
 *     (flammes de Curffe) passe dans le sort toutes les FeedInterval s, jusqu'à MaxFeed.
 *     Relâcher, atteindre le max ou épuiser la ressource enchaîne sur l'incantation.
 *     Le client décide du nombre ; le serveur le borne à sa ressource et au temps mesuré.
 *     Une seule barre de cast de l'appui au lancer : un cran par flamme, repliée à la fin du nourrissage.
 *  1. Si CastTime > 0 : incantation (barre de cast, ralenti), annulée par un contrôle dur
 *     (GenGameplayTags::GetHardCCTags) ou la mort, ou par un autre sort : lancer un sort à incantation
 *     annule l'incantation en cours du joueur (CancelOtherPendingCasts).
 *  2. Le client récupère le point visé sous la souris et l'envoie au serveur (target data,
 *     avec le nombre d'unités nourries) => on vise à la FIN de l'incantation.
 *     Serveur pour un client distant : pas de minuteur propre, c'est l'arrivée de la visée qui
 *     termine l'incantation (durée vérifiée à CastTimeTolerance près, voir GenFeeding::GetServerCastWait).
 *     Visée arrivée trop tôt : le serveur garde toute l'incantation jusqu'à sa propre fin (barre et effet vus
 *     par les autres, ralenti, interruption par un contrôle dur), puis lance le sort (coût, cooldown) et le fait
 *     partir. Une seule visée par activation ; les autres sorts du joueur ne peuvent plus annuler ce lancer.
 *  3. Lancer (ReleaseCast) : CommitAbility (cooldown, coût) + dépense de la ressource nourrie,
 *     le lanceur se tourne vers la visée, montage. Une incantation interrompue ne coûte rien.
 *  4. Départ (OnCastLaunched, sous-classe) : ce que fait le sort. La sous-classe appelle FinishAbility()
 *     quand elle a fini (tout de suite pour un projectile, plus tard pour une fenêtre de contre ou un bond).
 */
UCLASS(Abstract)
class GEN_API UGenGA_Cast : public UGenGameplayAbility
{
	GENERATED_BODY()

public:
	UGenGA_Cast();

	/** Serveur : compte exact annoncé par le client distant à la fin de son nourrissage (affichage seulement, borné par le temps). */
	void ApplyReportedFedCount(int32 Reported);

	/** Vrai pendant le nourrissage et l'incantation, avant le lancer. */
	bool IsCastPending() const { return IsActive() && !bReleased; }

	/** Serveur : visée reçue en avance, le lancer attend la fin de l'incantation mesurée par le serveur (tests). */
	bool IsWaitingForDeferredLaunch() const { return bServerShotLocked && PendingAimData.Num() > 0; }

	/**
	 * Client (ou hôte), au moment d'envoyer la visée : le sort y ajoute ce que le serveur doit reprendre à l'identique
	 * (bond : distance et lacet). Data.HitResult.Location = point visé. Par défaut : rien.
	 */
	virtual void FillAimData(FGenTargetData_Aim& Data) const {}

	//~ UGameplayAbility
	/** Faux sur le serveur entre la visée du client et le départ du sort : le client a déjà lancé. */
	virtual bool CanBeCanceled() const override;

protected:
	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;

	/**
	 * Le sort part (client et serveur ; coûts déjà payés). Appeler FinishAbility() quand il a fini.
	 * Par défaut : termine tout de suite.
	 */
	virtual void OnCastLaunched(const FGenCastRelease& Release);

	/** Faux pendant une phase qu'un contrôle dur ne coupe pas (ex : bond en vol). */
	virtual bool IsInterruptedByHardCC() const { return true; }

	/** Termine le sort. Seul le serveur réplique la fin (voir le commentaire dans le .cpp). */
	void FinishAbility();

	/** Serveur qui exécute le sort d'un client distant (ni hôte, ni autonome, ni IA). */
	bool IsServerForRemoteClient() const;

	/**
	 * Verrou de lancement (State.CastLocked, tag local sur le serveur et le client) ; retiré à la fin du sort.
	 * MinLockDuration : durée la PLUS COURTE possible du verrou (bond : son atterrissage le plus précoce, pas LeapDuration).
	 * Le serveur s'en sert pour ne refuser les sorts d'un client distant qu'au début du verrou (UGenAbilitySystemComponent::NoteCastLock).
	 * Seul point d'entrée du tag : poser State.CastLocked sans NoteCastLock laisse la fenêtre du serveur fermée.
	 */
	void SetCastLock(bool bLocked, float MinLockDuration = 0.f);

	/** Spec du GE de dégâts (SetByCaller.Damage = Amount, même 0 : tags et cues de l'effet). Invalide seulement sans EffectClass. */
	FGameplayEffectSpecHandle MakeDamageSpec(TSubclassOf<UGameplayEffect> EffectClass, float Amount, UObject* SourceObject) const;

	/** Spec des gains du lanceur (UGenGE_Gain). Invalide si rien à gagner. */
	FGameplayEffectSpecHandle MakeGainSpec(float Energy, float Resource) const;

	/** Serveur : projectile tiré depuis Origin vers Direction, porteur des dégâts et des gains. */
	AGenProjectile* SpawnProjectileShot(TSubclassOf<AGenProjectile> ShotClass, const FVector& Origin, const FVector& Direction, const FGenProjectileShotParams& ShotParams,
		TSubclassOf<UGameplayEffect> DamageClass, float DamageAmount, float EnergyGain, float ResourceGain, const TSharedPtr<FGenProjectileSalvo>& Salvo = nullptr);

	/** Serveur : zone au sol posée au sol sous Center, porteuse des dégâts et des gains (énergie si elle touche). */
	AGenGroundArea* SpawnGroundArea(TSubclassOf<AGenGroundArea> AreaClass, const FVector& Center, const FGenAreaParams& Params,
		TSubclassOf<UGameplayEffect> DamageClass, float DamageAmount, float EnergyGain);

	/** Fin de l'incantation (ou tout de suite si CastTime = 0) : on récupère la visée. */
	UFUNCTION()
	void OnCastFinished();

	/** Contrôle dur (étourdi, silence, peur, neutralisé) pendant le nourrissage, l'incantation ou une phase interruptible après le départ. */
	UFUNCTION()
	void OnCastInterrupted();

	UFUNCTION()
	void OnTargetDataReady(const FGameplayAbilityTargetDataHandle& DataHandle);

	/** Serveur pour un client distant : visée reçue pendant l'incantation. */
	UFUNCTION()
	void OnServerAimReceived(const FGameplayAbilityTargetDataHandle& DataHandle);

	/** Serveur : visée arrivée trop tôt, fin de l'incantation du serveur => le sort est lancé (coût, cooldown) et part. */
	UFUNCTION()
	void OnServerLaunchDelayFinished();

	UFUNCTION()
	void OnFeedTick();

	UFUNCTION()
	void OnFeedInputReleased(float TimeHeld);

	/** Fin du nourrissage, synchronisée client -> serveur. */
	UFUNCTION()
	void OnFeedSynced();

	/** Durée d'incantation en secondes (après le nourrissage). 0 = sort instantané. */
	UPROPERTY(EditDefaultsOnly, Category = "Cast", meta = (ClampMin = "0.0", Units = "s"))
	float CastTime = 0.f;

	/** Vitesse de déplacement pendant le nourrissage et l'incantation (1 = pas de ralenti, 0 = immobile). */
	UPROPERTY(EditDefaultsOnly, Category = "Cast", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float CastMoveSpeedMultiplier = 0.5f;

	/** Effet sur le lanceur pendant l'incantation (vu par tous) : annonce le sort et sa direction. */
	UPROPERTY(EditDefaultsOnly, Category = "Cast")
	TObjectPtr<UNiagaraSystem> CastFX;

	/** Socket du mesh où attacher CastFX. */
	UPROPERTY(EditDefaultsOnly, Category = "Cast")
	FName CastFXSocket = TEXT("hand_r");

	/** Le lanceur se tourne vers la visée au lancer. */
	UPROPERTY(EditDefaultsOnly, Category = "Cast")
	bool bTurnToAim = true;

	/**
	 * Montage d'incantation (optionnel, répliqué par le GAS) : joué dès le début de l'incantation (après le nourrissage).
	 * Avec un CastMontage : préparation seule, calée sur CastTime (bScaleChargeMontageToCastTime).
	 * Sans CastMontage (montage unique du Plan 1) : préparation puis geste de lancer, à vitesse 1 ; le régler pour que
	 * le lancer tombe à CastTime. Coupé si l'incantation est interrompue. Ignoré si CastTime = 0 (utiliser CastMontage).
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Cast|Animation", meta = (EditCondition = "CastTime > 0"))
	TObjectPtr<UAnimMontage> ChargeMontage;

	/** Montage de lancer (optionnel, répliqué aux autres joueurs par le GAS). Joué au lancer. */
	UPROPERTY(EditDefaultsOnly, Category = "Cast|Animation")
	TObjectPtr<UAnimMontage> CastMontage;

	/**
	 * Échelle du mouvement racine des montages du sort (ChargeMontage, CastMontage, et LandMontage du bond).
	 * 1 = celui du clip. 0 quand le déplacement vient d'une Root Motion Source (bond : ApplyRootMotionJumpForce) :
	 * Art Bible §8.4, un clip à mouvement racine passe AnimRootMotionTranslationScale = 0.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Cast|Animation", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float CastMontageRootMotionScale = 1.f;

	// --- Plan Visuals V3 : phases de montage calées sur le sort (Feed -> Charge -> Cast) ---
	// Sans FeedMontage ni CastMontage, le comportement du Plan 1 reste : ChargeMontage unique à vitesse 1.

	/**
	 * Montage de nourrissage (optionnel, répliqué par le GAS) : joué dès le début du nourrissage, pour que tout le monde
	 * voie le geste (Art Bible §12 Q41). Sections Feed_1..Feed_N d'UN intervalle de base chacune, enchaînées, la dernière
	 * en boucle (sécurité) : à la bonne vitesse les frontières tombent sur les seuils. Vitesse = longueur de Feed_1 /
	 * intervalle actif (x2 en nourrissage rapide). Python : feed_montage.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Cast|Animation", meta = (EditCondition = "bFeedable"))
	TObjectPtr<UAnimMontage> FeedMontage;

	/**
	 * ChargeMontage dure exactement CastTime (vitesse = longueur / CastTime, Art Bible §8.3). Seulement avec un
	 * CastMontage à part : sinon ChargeMontage est l'ancien montage unique (préparation + lancer), joué à vitesse 1
	 * (GenMontageTiming::ShouldScaleChargeToCastTime). Python : scale_charge_montage_to_cast_time.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Cast|Animation")
	bool bScaleChargeMontageToCastTime = true;

	/**
	 * Coupe CastMontage quand le sort se termine (posture tenue : contre). Faux = le geste continue après le sort.
	 * Python : stop_cast_montage_with_ability.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Cast|Animation")
	bool bStopCastMontageWithAbility = false;

	/**
	 * Durée de jeu de la phase lancée, à laquelle CastMontage est calé (0 = vitesse 1 : geste au lancer puis suivi).
	 * Surcharges prévues : bond -> durée du vol ; contre -> fenêtre ; Living Flame -> forme.
	 */
	virtual float GetCastMontageTargetDuration() const { return 0.f; }

	/**
	 * Joue un montage de phase à Rate (répliqué aux autres joueurs par le GAS), avec CastMontageRootMotionScale.
	 * nullptr si Montage est nul (asset pas encore créé) : l'appelant n'a rien d'autre à faire.
	 */
	UAbilityTask_PlayMontageAndWait* PlayPhaseMontage(UAnimMontage* Montage, float Rate, bool bStopWhenAbilityEnds);

	/** Vitesse calée sur TargetDuration ; avertit si le clip devrait être recalé (hors Shipping). */
	float GetPhaseRate(const UAnimMontage* Montage, float AuthoredLength, float TargetDuration, float ExpectedRate) const;

	/** Maintenir la touche nourrit le sort avec la ressource du champion (attribut Resource). */
	UPROPERTY(EditDefaultsOnly, Category = "Cast|Feeding")
	bool bFeedable = false;

	/** Une unité absorbée toutes les FeedInterval secondes. Par défaut : CurffeTuning::FeedInterval (0.3 s). */
	UPROPERTY(EditDefaultsOnly, Category = "Cast|Feeding", meta = (EditCondition = "bFeedable", ClampMin = "0.05", Units = "s"))
	float FeedInterval;

	/**
	 * Seuils de nourrissage du sort (crans de la barre), même si le champion a plus de ressource.
	 * Par défaut : CurffeTuning::MaxFeedPerSpell (3).
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Cast|Feeding", meta = (EditCondition = "bFeedable", ClampMin = "1"))
	int32 MaxFeed;

private:
	void StartFeeding();
	void ScheduleFeedTick();
	/** V3 : joue FeedMontage (rate calée sur l'intervalle actif, Feed_1 trouvée par son nom). */
	void PlayFeedMontage();
	/** Revue V2-V4, I2 : serveur pour un client distant, geste de nourrissage lancé avec le retard de l'estimation. */
	UFUNCTION()
	void OnFeedMontageDelayFinished();
	/** Revue V2-V4, I3 : arrête FeedMontage s'il joue encore (courant : arrêt répliqué ; sinon arrêt local). */
	void StopFeedMontage();
	/**
	 * Revue V2-V4, I1 : serveur pour un client distant, State.FastFeeding retiré juste avant l'activation (ou posé juste
	 * après) => le client appuyait avec ; on prend l'intervalle rapide.
	 */
	bool IsFastFeedingInGrace(bool bAddedAfterActivation) const;
	/** Client (ou hôte) : fin du nourrissage => prévient le serveur puis incante. */
	void StopFeedingLocal();
	void EndFeedTasks();
	void SetFedVisual(int32 Count);
	/**
	 * Serveur pour un client distant : recalcule l'affichage (estimation ou annonce du client bornée par le temps,
	 * jamais en recul, GenFeeding::ReconcileDisplayedFed), l'applique et le renvoie.
	 */
	int32 ReconcileFedVisual();
	/** Barre de cast : fin du nourrissage avec Count unités (rappelable pour corriger le compte, sans recul sauf bFinal). */
	void MarkFeedEnded(int32 Count, bool bFinal = false);
	int32 GetAvailableFeed() const;

	void StartCasting();
	void ApplyCastSlow();
	void StartInterruptWatch();
	/** Retire le ralenti et la barre de cast (garde le visuel des unités nourries). */
	void EndCastPresentation();
	/** Nettoyage complet (fin ou annulation du sort). */
	void StopCasting();

	/** Lancer d'un autre sort à incantation du joueur : celui-ci remplace l'incantation en cours. */
	void CancelOtherPendingCasts();

	/**
	 * Serveur pour un client distant, lancer refusé (visée invalide, CommitAbility refusé) : le client a déjà joué
	 * son geste. On le coupe chez lui (Art Bible §8.4 : aucune clé de prédiction n'est rejetée à ce stade).
	 */
	void StopClientCastMontages();

	/**
	 * Échec ou abandon d'un lancer (visée invalide, commit refusé, contrôle dur ou mort pendant le départ différé) :
	 * coupe le geste chez le client propriétaire ET, sur le serveur, le montage répliqué aux autres joueurs.
	 */
	void AbortCastMontages();

	/** Arrête d'écouter la visée (après la première : les suivantes sont ignorées). */
	void EndAimTask();

	/**
	 * Lancer : borne le nourrissage, CommitAbility (cooldown, coût), dépense la ressource, tourne le lanceur,
	 * joue le montage. Faux si le sort a été terminé (visée invalide, commit refusé).
	 */
	bool ReleaseCast(const FGameplayAbilityTargetDataHandle& DataHandle, FGenCastRelease& OutRelease);
	/** Départ : OnCastLaunched, puis le sort redevient annulable (mort, autre sort) s'il reste actif. */
	void LaunchCast(const FGenCastRelease& Release);

	/** Nombre d'unités nourries retenu pour ce lancer (borné côté serveur). bLog = faux : sans journal (affichage). */
	int32 ResolveFedCount(const FGameplayAbilityTargetData* Data, bool bLog = true) const;

	/** Serveur : journal d'une visée en avance (Warning limité si elle est très en avance, GenFeeding::IsAimSuspiciouslyEarly). */
	void LogEarlyAim(float Elapsed, float Wait) const;

	/** Serveur : acquitte la clé de prédiction d'une visée différée (le client retire ses valeurs prédites). */
	void AcknowledgeDeferredAim();
	void SpendResource(int32 Amount);

	/**
	 * Ralenti de l'incantation posé (multiplicateur local de AGenCharacterBase). Revue Plan 2 Tasks 7-8, I-4 : plus de GE
	 * prédit, chaque machine le pose à SON début et le retire à SA fin, comme ses propres mouvements (aucune correction).
	 */
	bool bCastSlowApplied = false;

	UPROPERTY(Transient)
	TObjectPtr<UAbilityTask_WaitDelay> FeedTickTask;

	UPROPERTY(Transient)
	TObjectPtr<UAbilityTask_WaitInputRelease> FeedReleaseTask;

	/** Revue V2-V4, I2 : attente avant le geste de nourrissage du serveur (ServerEstimateLag), arrêtée par EndFeedTasks. */
	UPROPERTY(Transient)
	TObjectPtr<UAbilityTask_WaitDelay> FeedMontageDelayTask;

#if !UE_BUILD_SHIPPING
	/** Revue V2-V4, I3 : avertissement "phases dans des groupes de slots différents" déjà donné pour cette instance. */
	bool bWarnedPhaseSlotGroups = false;
#endif
	/** Tâche de visée en cours (serveur pour un client distant : attend la visée). */
	UPROPERTY(Transient)
	TObjectPtr<UGenAbilityTask_TargetDataUnderCursor> AimTask;

	int32 FedCount = 0;
	int32 FedVisualCount = 0;
	/** Unités disponibles à l'appui (crans de la barre) : plafond du nourrissage. */
	int32 FeedSlotsAtPress = 0;
	bool bIsFeeding = false;
	bool bInterruptWatchStarted = false;
	/** Le sort a été lancé (coûts payés) : il n'est plus "en incantation". */
	bool bReleased = false;
	bool bCastLockApplied = false;
	float FeedStartTime = 0.f;
	/**
	 * Intervalle retenu au début du nourrissage (rapide sous State.FastFeeding), sur chaque machine. Posé par StartFeeding.
	 * Sert aux ticks, à la barre de cast et à la validation du serveur : jamais FeedInterval directement pendant un sort.
	 */
	float ActiveFeedInterval = 0.f;
	/** Serveur : durée du nourrissage mesurée entre l'activation et le signal du client. */
	float ServerFeedElapsed = 0.f;
	/** Serveur : compte brut annoncé par le client (ServerReportFedResource), sinon INDEX_NONE. */
	int32 ReportedFedCount = INDEX_NONE;

	/** Début de l'incantation (après le nourrissage), en temps du monde. */
	float CastStartTime = 0.f;
	/**
	 * Serveur : visée du client reçue, le sort est parti côté client (plus annulable par un autre sort du joueur).
	 * Levé au départ (LaunchCast) : un sort qui reste actif après (fenêtre de contre, bond) redevient annulable.
	 */
	bool bServerShotLocked = false;
	/** Serveur : visée reçue en avance, traitée (coût, cooldown, départ) à la fin de l'incantation du serveur. */
	FGameplayAbilityTargetDataHandle PendingAimData;
	/** Serveur : clé de prédiction de la visée différée, acquittée au départ du sort ou à son annulation. */
	FPredictionKey DeferredAimKey;
};
