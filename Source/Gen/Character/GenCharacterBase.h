#pragma once

#include "CoreMinimal.h"
#include "AbilitySystemInterface.h"
#include "ActiveGameplayEffectHandle.h"
#include "GameFramework/Character.h"
#include "GameplayAbilitySpecHandle.h"
#include "AbilitySystem/GenFeeding.h"
#include "AbilitySystem/GenHitRules.h"
#include "Character/GenStatusVisualsComponent.h"
#include "GenCharacterBase.generated.h"

class UAnimMontage;
class UGameplayEffect;
class UNiagaraComponent;
class UNiagaraSystem;
class UGenAbilitySystemComponent;
class UGenAttributeSet;
class UGenGameplayAbility;
class UGenSpellIndicatorComponent;
struct FOnAttributeChangeData;

namespace GenCastBar
{
	struct FLayout;
}

/** Compte affiché d'unités nourries qui change (ancien, nouveau). Toutes les machines sauf le serveur dédié. */
DECLARE_MULTICAST_DELEGATE_ThreeParams(FGenFedResourceChanged, AGenCharacterBase* /*Character*/, int32 /*Old*/, int32 /*New*/);

/**
 * Seuil de nourrissage franchi (le compte affiché AUGMENTE, GenIndicatorRules::IsThresholdPop) : nouveau compte.
 * Émis au même moment que le GameplayCue local GameplayCue.Feed.Threshold. Toutes les machines sauf le serveur dédié.
 */
DECLARE_MULTICAST_DELEGATE_TwoParams(FGenFedThresholdReached, AGenCharacterBase* /*Character*/, int32 /*NewCount*/);

/** Équipe "neutre" : ennemie de tout le monde (mannequins d'entraînement, monstres...). */
inline constexpr uint8 GenNoTeam = 255;

/** Incantation en cours (affichée par le HUD : barre de cast). */
USTRUCT(BlueprintType)
struct FGenCastInfo
{
	GENERATED_BODY()

	/** Classe du sort incanté (nullptr = pas d'incantation). Le HUD y lit le nom affiché. */
	UPROPERTY(BlueprintReadOnly, Category = "Cast")
	TObjectPtr<UClass> Ability;

	/** Début de l'incantation, en temps serveur (GameState::GetServerWorldTimeSeconds). */
	UPROPERTY(BlueprintReadOnly, Category = "Cast")
	float StartTime = 0.f;

	/** Durée de la barre. Sort nourri : durée maximale (FeedSlots × FeedInterval + incantation). */
	UPROPERTY(BlueprintReadOnly, Category = "Cast")
	float Duration = 0.f;

	/** Effet joué sur le lanceur pendant l'incantation (ex: feu qui se forme dans la main), vu par tous. */
	UPROPERTY(BlueprintReadOnly, Category = "Cast")
	TObjectPtr<UNiagaraSystem> FX;

	/** Socket du mesh où attacher FX (ex: hand_r). */
	UPROPERTY(BlueprintReadOnly, Category = "Cast")
	FName FXSocket;

	/** Sort nourri : flammes disponibles à l'appui, un cran chacune (0 = incantation normale). */
	UPROPERTY(BlueprintReadOnly, Category = "Cast")
	uint8 FeedSlots = 0;

	/** Sort nourri : une flamme toutes les FeedInterval s. */
	UPROPERTY(BlueprintReadOnly, Category = "Cast")
	float FeedInterval = 0.f;

	/** Sort nourri : fin du nourrissage, en temps serveur (0 = nourrissage en cours). */
	UPROPERTY(BlueprintReadOnly, Category = "Cast")
	float FeedEndTime = 0.f;

	/** Sort nourri : flammes nourries, connues à la fin du nourrissage. */
	UPROPERTY(BlueprintReadOnly, Category = "Cast")
	uint8 FedCount = 0;

	/** Canalisation (fenêtre minutée : contre, forme de Living Flame) : la barre se vide (UI §4.5). Posé par StartChannel. */
	UPROPERTY(BlueprintReadOnly, Category = "Cast")
	bool bChannel = false;

	bool IsCasting() const { return Ability != nullptr && Duration > 0.f; }
};

/**
 * Point d'atterrissage d'un bond en vol, vu par TOUS les joueurs (décision du 2026-10-08 : les ennemis voient le cercle
 * d'atterrissage pendant le vol). Posé au départ du bond (serveur et client propriétaire), effacé à l'atterrissage.
 * Les indicateurs (cercle, amorces de l'anneau) le lisent chaque image ; le propriétaire a aussi sa propre visée.
 */
USTRUCT(BlueprintType)
struct FGenLeapTarget
{
	GENERATED_BODY()

	/** Classe du sort de bond (nullptr = pas de bond en vol). */
	UPROPERTY(BlueprintReadOnly, Category = "Leap")
	TObjectPtr<UClass> Ability;

	/** Point visé, ramené à la portée (l'atterrissage réel peut être plus court : marche, rebord). */
	UPROPERTY(BlueprintReadOnly, Category = "Leap")
	FVector_NetQuantize10 Location = FVector::ZeroVector;

	/** Direction horizontale du bond : première direction de l'anneau (GenAreaRules::GetRingDirections). */
	UPROPERTY(BlueprintReadOnly, Category = "Leap")
	FVector_NetQuantizeNormal Direction = FVector::ForwardVector;

	/** Rayon de la zone d'atterrissage (cm) : le cercle = la hitbox. */
	UPROPERTY(BlueprintReadOnly, Category = "Leap")
	float Radius = 0.f;

	/** Unités nourries (une boule de l'anneau chacune). */
	UPROPERTY(BlueprintReadOnly, Category = "Leap")
	uint8 Fed = 0;

	/** Départ du bond, en temps serveur (GameState::GetServerWorldTimeSeconds). */
	UPROPERTY(BlueprintReadOnly, Category = "Leap")
	float StartTime = 0.f;

	bool IsActive() const { return Ability != nullptr; }
};

/**
 * Classe de base de tout ce qui a des PV et des sorts (champions, mannequins...).
 *
 * L'ASC n'appartient pas forcément au personnage : pour les joueurs il vit sur le
 * PlayerState (il survit au respawn), pour les PNJ sur le personnage lui-même.
 * Les classes enfants renseignent AbilitySystemComponent/AttributeSet puis appellent
 * OnAbilitySystemInitialized().
 */
UCLASS(Abstract)
class GEN_API AGenCharacterBase : public ACharacter, public IAbilitySystemInterface
{
	GENERATED_BODY()

public:
	AGenCharacterBase(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	//~ IAbilitySystemInterface
	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;

	UGenAbilitySystemComponent* GetGenAbilitySystemComponent() const { return AbilitySystemComponent; }
	UGenAttributeSet* GetAttributeSet() const { return AttributeSet; }

	/** Équipe du personnage (GenNoTeam = neutre, hostile à tous). */
	virtual uint8 GetTeamId() const { return GenNoTeam; }

	UFUNCTION(BlueprintPure, Category = "Gen|Team", meta = (DisplayName = "Get Team Id"))
	int32 K2_GetTeamId() const { return GetTeamId(); }

	static bool AreTeamsEnemies(uint8 TeamA, uint8 TeamB);
	static bool AreEnemies(const AActor* A, const AActor* B);

	UFUNCTION(BlueprintPure, Category = "Gen|Health")
	bool IsDead() const { return bIsDead; }

	/**
	 * Intouchable (State.Untouchable) : les coups le traversent (ResolveIncomingHit renvoie Ignored), dégâts, contrôles
	 * durs (ApplyHardCC) et repoussements (ApplyKnockback) ignorés. N'empêche pas de lancer : Living Flame pose en plus
	 * State.CastLocked.
	 */
	UFUNCTION(BlueprintPure, Category = "Gen|Health")
	bool IsUntouchable() const;

	UFUNCTION(BlueprintPure, Category = "Gen|Health")
	float GetHealth() const;

	UFUNCTION(BlueprintPure, Category = "Gen|Health")
	float GetMaxHealth() const;

	UFUNCTION(BlueprintPure, Category = "Gen|Health")
	float GetEnergy() const;

	UFUNCTION(BlueprintPure, Category = "Gen|Health")
	float GetMaxEnergy() const;

	UFUNCTION(BlueprintPure, Category = "Gen|Resource")
	float GetResource() const;

	UFUNCTION(BlueprintPure, Category = "Gen|Resource")
	float GetMaxResource() const;

	/** Unités de ressource en train d'être nourries dans un sort : elles quittent l'orbite à l'écran. */
	UFUNCTION(BlueprintPure, Category = "Gen|Resource")
	int32 GetFedResource() const { return FedResource; }

	/**
	 * Appelé par le sort qui nourrit (Source), sur le serveur et le client propriétaire (prédiction).
	 * Un seul sort possède l'affichage : Count = 0 n'efface que l'affichage posé par Source.
	 */
	void SetFedResource(const UObject* Source, uint8 Count);

	/** Le sort Source disparaît (retiré, ramassé) : s'il possède l'affichage, il est effacé (sinon rien ne l'effacerait). */
	void ClearFedResourceFrom(const UObject* Source);

	/** Efface l'affichage quel que soit son propriétaire (mort, réapparition). */
	void ResetFedResource();

	// --- Plan Visuals V2 : seuils de nourrissage (cosmétique, jamais sur le serveur dédié) ---

	/** Seuils de nourrissage pour les cosmétiques (Foyer, charge de la main, indicateurs). Rien sur un serveur dédié. */
	FGenFedResourceChanged OnFedResourceChanged;

	/**
	 * Pop d'un seuil (compte en hausse seulement), avec le compte ABSOLU atteint : une réplication regroupée (0 -> 2)
	 * ne donne qu'un pop, qui doit donc se dimensionner sur ce compte, jamais sur "+1". Même contrat pour le GameplayCue
	 * GameplayCue.Feed.Threshold (RawMagnitude = compte absolu). Rien sur un serveur dédié.
	 */
	FGenFedThresholdReached OnFedThresholdReached;

	/**
	 * Client propriétaire -> serveur : nombre exact d'unités nourries par Ability à la fin du nourrissage.
	 * Purement visuel (les autres joueurs voient les flammes quitter l'orbite) : sans cela le serveur
	 * n'affiche que sa propre estimation, qui peut avoir un tick de retard. Transmis au sort actif, qui
	 * l'accepte à ±1 de son estimation (UGenGA_Cast::ApplyReportedFedCount). Le nombre qui compte
	 * pour le tir arrive avec la visée et y est validé.
	 */
	UFUNCTION(Server, Reliable)
	void ServerReportFedResource(UClass* Ability, uint8 Count);

	/**
	 * Multiplicateur de vitesse posé par un sort (Source, Reason), local à cette machine et jamais répliqué : le serveur et
	 * le client propriétaire le posent chacun au début de LEUR phase et le retirent à LEUR fin (ralenti d'incantation,
	 * posture de contre). La vitesse de marche = attribut MoveSpeed × produit de ces multiplicateurs. Revue Plan 2
	 * Tasks 7-8, I-4 : un GE prédit restait ~1 RTT de trop chez le client (le retrait du serveur) => correction du
	 * mouvement à chaque fin de phase. Les autres clients n'en ont pas besoin (mouvement répliqué).
	 */
	void SetLocalMoveSpeedMultiplier(const UObject* Source, FName Reason, float Multiplier);
	void ClearLocalMoveSpeedMultiplier(const UObject* Source, FName Reason);

	/** Produit des multiplicateurs locaux (1 = aucun). */
	UFUNCTION(BlueprintPure, Category = "Gen|Movement")
	float GetLocalMoveSpeedMultiplier() const;

	/**
	 * Serveur : repousse le personnage de Distance (cm) dans Direction (aplatie à l'horizontale).
	 * Le client propriétaire reçoit le même lancement pour éviter une correction brutale.
	 */
	void ApplyKnockback(const FVector& Direction, float Distance);

	/**
	 * Serveur : un coup ennemi de nature Kind arrive (Source = projectile, zone...). À appeler par toute
	 * source de dégâts AVANT d'appliquer quoi que ce soit. Countered : le coup n'inflige rien, et le
	 * contre actif reçoit Event.Counter.Blocked (Instigator = Attacker, peut être nul ; EventMagnitude =
	 * GenHitRules::ToEventMagnitude(Kind), jamais 0). Ignored : cible intouchable, le coup la traverse.
	 * Seul un ennemi déclenche un contre (un attaquant nul, déjà détruit, compte comme ennemi).
	 * Futures attaques de mêlée : Kind = Melee.
	 */
	EGenHitResponse ResolveIncomingHit(AActor* Attacker, EGenHitKind Kind, const UObject* Source);

	/**
	 * Incantation : appelés par les sorts sur le serveur ET le client propriétaire (prédiction).
	 * Répliqué aux autres clients pour afficher la barre de cast et l'effet des ennemis.
	 * Pendant l'incantation, le personnage se tourne vers la visée du joueur (rotation de contrôle)
	 * au lieu de suivre son déplacement.
	 */
	void StartCast(UClass* Ability, float Duration, UNiagaraSystem* FX = nullptr, FName FXSocket = NAME_None);
	void StopCast(UClass* Ability);

	/**
	 * Sort nourri : une seule barre de l'appui au lancer. Démarre la barre du nourrissage
	 * (FeedSlots crans, longueur FeedSlots × FeedInterval + CastTime). Arrêtée par StopCast.
	 */
	void StartFeedCast(UClass* Ability, int32 FeedSlots, float FeedInterval, float CastTime, UNiagaraSystem* FX = nullptr, FName FXSocket = NAME_None);

	/**
	 * Fin du nourrissage avec FedCount flammes : les segments inutilisés se replient et la même barre
	 * continue sur l'incantation. Rappelable pour corriger le compte (l'heure de fin reste la première) :
	 * une correction ne fait jamais reculer le compteur, sauf bFinal (compte validé au lancer, visée reçue).
	 * Sans effet si la barre n'est plus celle d'Ability.
	 */
	void MarkFeedEnded(UClass* Ability, int32 FedCount, bool bFinal = false);

	/**
	 * Plan Visuals V4 : fenêtre minutée affichée comme une canalisation (la barre se vide de droite à gauche, UI §4.5),
	 * vue par tous : fenêtre de contre, forme de Living Flame. Serveur et client propriétaire, comme StartCast, mais
	 * sans effet d'incantation ni visée imposée (SetFaceAim). Arrêtée par StopCast(Ability).
	 * À appeler depuis OnCastLaunched : UGenGA_Cast a déjà retiré la barre de l'incantation (EndCastPresentation).
	 */
	void StartChannel(UClass* Ability, float Duration);

	/**
	 * Part écoulée de l'incantation ou de la canalisation en cours, 0..1 (0 sans incantation), en temps serveur.
	 * Grandit même quand la barre se vide : horloge des télégraphes centrés (UGenSpellIndicatorComponent).
	 * TODO (revue V2-V4, M8) : faux pour un sort nourri après le repli (Duration = nourrissage complet + incantation) ;
	 * aucun télégraphe centré n'est nourri aujourd'hui. Passer par GetCastBarLayout le jour où il y en aura un.
	 */
	float GetCastElapsedFraction() const;

	const FGenCastInfo& GetCastInfo() const { return CastInfo; }

	/** Horloge des incantations et des bonds : temps serveur (GameState), sinon temps du monde. */
	float GetCastClockSeconds() const;

	/** Plan Visuals V6 : indicateurs au sol (visée du lanceur, télégraphes centrés, bond en vol). */
	UGenSpellIndicatorComponent* GetSpellIndicator() const { return SpellIndicator; }

	/** Bond en vol : point d'atterrissage vu par tous (serveur et client propriétaire ; StartTime est fixé ici). */
	void SetLeapTarget(const FGenLeapTarget& Target);
	/** Fin du bond d'Ability (sans effet si un autre bond a pris la place). */
	void ClearLeapTarget(UClass* Ability);
	const FGenLeapTarget& GetLeapTarget() const { return LeapTarget; }

	/**
	 * Remplissage de la barre de cast, 0..1, ou -1 si aucune incantation. Pour une canalisation (bChannel), c'est la
	 * part RESTANTE (la barre se vide, UI §4.5) ; la part écoulée est GetCastElapsedFraction.
	 */
	UFUNCTION(BlueprintPure, Category = "Gen|Cast")
	float GetCastProgress() const;

	/** Disposition complète de la barre (remplissage, crans, compteur). Faux si aucune incantation. */
	bool GetCastBarLayout(GenCastBar::FLayout& OutLayout) const;

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Appelé quand l'ASC et l'avatar sont prêts, sur le serveur ET les clients. */
	virtual void OnAbilitySystemInitialized();

	/** Débranche les delegates et (serveur) retire les sorts/effets donnés par ce personnage. */
	virtual void UninitializeAbilitySystem();

	/** Serveur : vie à 0. */
	virtual void HandleOutOfHealth(AActor* DamageInstigator, AActor* DamageCauser);

	/** Toutes les machines : désactive mouvement/collisions, ragdoll. */
	virtual void OnDeathStarted();

	UFUNCTION()
	void OnRep_IsDead();

	UFUNCTION(BlueprintImplementableEvent, Category = "Gen|Health", meta = (DisplayName = "On Death"))
	void K2_OnDeath();

	/** Sorts donnés au personnage à l'apparition (chaque sort porte son InputTag). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gen|Abilities")
	TArray<TSubclassOf<UGenGameplayAbility>> StartupAbilities;

	/** Effets appliqués à l'apparition (ex: GE instantané de stats de départ du champion, passifs). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gen|Abilities")
	TArray<TSubclassOf<UGameplayEffect>> StartupEffects;

	/** Formes d'état (contre, étourdi...) affichées sur ce personnage. Python : status_visual_config. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gen|Status")
	TArray<FGenStatusVisual> StatusVisualConfig;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Gen|Status")
	TObjectPtr<UGenStatusVisualsComponent> StatusVisuals;

	/** Indicateurs au sol (matériaux MI_Telegraph_* assignés dans BP_Champion). Rien sur un serveur dédié. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Gen|Indicator")
	TObjectPtr<UGenSpellIndicatorComponent> SpellIndicator;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gen|Health")
	bool bRagdollOnDeath = true;

	UPROPERTY(ReplicatedUsing = OnRep_IsDead, BlueprintReadOnly, Category = "Gen|Health")
	bool bIsDead = false;

	/** Non répliqué au propriétaire : il le prédit lui-même. */
	UPROPERTY(ReplicatedUsing = OnRep_CastInfo, BlueprintReadOnly, Category = "Gen|Cast")
	FGenCastInfo CastInfo;

	/** Non répliqué au propriétaire : il le prédit lui-même. Les autres clients en tirent les seuils (OnRep_FedResource). */
	UPROPERTY(ReplicatedUsing = OnRep_FedResource, BlueprintReadOnly, Category = "Gen|Resource")
	uint8 FedResource = 0;

	/**
	 * Échelle de l'effet d'incantation par unité nourrie (Curffe : la charge de la main grandit d'un cran par flamme,
	 * Curffe-Visuals.md §3.2). 0 = taille fixe. Python : cast_fx_scale_per_fed.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gen|Cast")
	float CastFXScalePerFed = 0.f;

	/** Autres clients : le compte nourri répliqué change (seuils, échelle de l'effet). */
	UFUNCTION()
	void OnRep_FedResource(uint8 OldValue);

	/**
	 * Toutes les écritures de FedResource y passent (serveur, client propriétaire, OnRep des autres clients).
	 * Diffuse le changement, met l'effet d'incantation à l'échelle et joue le pop local du seuil. Rien sur un serveur dédié.
	 */
	void NotifyFedResourceChanged(int32 Old, int32 New, bool bAllowPop = true);

	/** Échelle de CastFXComponent pour Count unités nourries (1 + CastFXScalePerFed × Count). */
	void ApplyCastFXScale(int32 Count);

	/** Non répliqué au propriétaire : il le prédit lui-même. */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Gen|Leap")
	FGenLeapTarget LeapTarget;

	/** Propriétaire de l'affichage des unités nourries (serveur et client propriétaire, non répliqué). */
	GenFeeding::FFedDisplay FedDisplay;

	/**
	 * Autres clients (revue V2-V4, M1) : début (StartTime) de l'incantation vue à la dernière réception de FedResource,
	 * -1 = jamais. Un compte reçu pour une autre incantation repart de 0 : le premier seuil du nouveau sort fait son pop.
	 */
	float FedRepCastStartTime = -1.f;

	/**
	 * Autres clients (revue V2-V4, M2) : image de la dernière réception de CastInfo. Un compte reçu dans la même image
	 * (personnage devenu pertinent en plein nourrissage, arrivée en cours de partie) ne fait pas de pop.
	 */
	uint64 CastInfoRepFrame = 0;

	/** Unités nourries par l'incantation EN COURS (0 si l'affichage appartient à un autre sort) : taille de l'effet. */
	uint8 GetFedCountForCurrentCast() const;

	UFUNCTION(Client, Reliable)
	void ClientApplyKnockback(FVector_NetQuantize10 LaunchVelocity);

public:
	/**
	 * Serveur -> client propriétaire : le serveur a refusé le lancer (visée invalide, CommitAbility refusé) alors que
	 * le client joue déjà son geste. Coupe Montage s'il joue encore (Art Bible §8.4, mauvaise prédiction).
	 */
	UFUNCTION(Client, Reliable)
	void ClientStopCastMontage(UAnimMontage* Montage);

protected:

	UFUNCTION()
	void OnRep_CastInfo();

	/** Lance ou arrête l'effet d'incantation selon CastInfo (rien sur un serveur dédié). */
	void UpdateCastFX();

	/** Pendant l'incantation : face à la visée (rotation de contrôle) ; sinon : face au déplacement. */
	void SetFaceAim(bool bFaceAim);

	UPROPERTY(Transient)
	TObjectPtr<UNiagaraComponent> CastFXComponent;

	/** Pointeurs mis en cache (l'ASC peut appartenir au PlayerState). */
	UPROPERTY(Transient)
	TObjectPtr<UGenAbilitySystemComponent> AbilitySystemComponent;

	UPROPERTY(Transient)
	TObjectPtr<UGenAttributeSet> AttributeSet;

private:
	void GrantStartupAbilitiesAndEffects();
	void RemoveStartupAbilitiesAndEffects();
	void OnMoveSpeedChanged(const FOnAttributeChangeData& Data);

	/** Vitesse de marche du CMC = MoveSpeed × GetLocalMoveSpeedMultiplier(). */
	void RefreshMaxWalkSpeed();

	struct FLocalMoveSpeedMultiplier
	{
		FObjectKey Source;
		FName Reason;
		float Multiplier = 1.f;
	};
	TArray<FLocalMoveSpeedMultiplier, TInlineAllocator<2>> LocalMoveSpeedMultipliers;

	TArray<FGameplayAbilitySpecHandle> GrantedAbilityHandles;
	TArray<FActiveGameplayEffectHandle> GrantedEffectHandles;
	FDelegateHandle MoveSpeedChangedHandle;
	FDelegateHandle OutOfHealthHandle;
};
