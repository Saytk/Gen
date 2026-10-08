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

class UGameplayEffect;
class UNiagaraComponent;
class UNiagaraSystem;
class UGenAbilitySystemComponent;
class UGenAttributeSet;
class UGenGameplayAbility;
struct FOnAttributeChangeData;

namespace GenCastBar
{
	struct FLayout;
}

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

	bool IsCasting() const { return Ability != nullptr && Duration > 0.f; }
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

	/** Intouchable (State.Untouchable) : les coups le traversent (ResolveIncomingHit renvoie Ignored). */
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

	/**
	 * Client propriétaire -> serveur : nombre exact d'unités nourries par Ability à la fin du nourrissage.
	 * Purement visuel (les autres joueurs voient les flammes quitter l'orbite) : sans cela le serveur
	 * n'affiche que sa propre estimation, qui peut avoir un tick de retard. Transmis au sort actif, qui
	 * l'accepte à ±1 de son estimation (UGenGA_Projectile::ApplyReportedFedCount). Le nombre qui compte
	 * pour le tir arrive avec la visée et y est validé.
	 */
	UFUNCTION(Server, Reliable)
	void ServerReportFedResource(UClass* Ability, uint8 Count);

	/**
	 * Serveur : repousse le personnage de Distance (cm) dans Direction (aplatie à l'horizontale).
	 * Le client propriétaire reçoit le même lancement pour éviter une correction brutale.
	 */
	void ApplyKnockback(const FVector& Direction, float Distance);

	/**
	 * Serveur : un coup ennemi de nature Kind arrive (Source = projectile, zone...). À appeler par toute
	 * source de dégâts AVANT d'appliquer quoi que ce soit. Countered : le coup n'inflige rien, et le
	 * contre actif reçoit Event.Counter.Blocked (Instigator = Attacker, EventMagnitude = Kind).
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
	 * continue sur l'incantation. Rappelable pour corriger le compte (l'heure de fin reste la première).
	 * Sans effet si la barre n'est plus celle d'Ability.
	 */
	void MarkFeedEnded(UClass* Ability, int32 FedCount);

	const FGenCastInfo& GetCastInfo() const { return CastInfo; }

	/** 0..1, ou -1 si aucune incantation. */
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

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gen|Health")
	bool bRagdollOnDeath = true;

	UPROPERTY(ReplicatedUsing = OnRep_IsDead, BlueprintReadOnly, Category = "Gen|Health")
	bool bIsDead = false;

	/** Non répliqué au propriétaire : il le prédit lui-même. */
	UPROPERTY(ReplicatedUsing = OnRep_CastInfo, BlueprintReadOnly, Category = "Gen|Cast")
	FGenCastInfo CastInfo;

	/** Non répliqué au propriétaire : il le prédit lui-même. */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Gen|Resource")
	uint8 FedResource = 0;

	/** Propriétaire de l'affichage des unités nourries (serveur et client propriétaire, non répliqué). */
	GenFeeding::FFedDisplay FedDisplay;

	UFUNCTION(Client, Reliable)
	void ClientApplyKnockback(FVector_NetQuantize10 LaunchVelocity);

	UFUNCTION()
	void OnRep_CastInfo();

	/** Lance ou arrête l'effet d'incantation selon CastInfo (rien sur un serveur dédié). */
	void UpdateCastFX();

	/** Horloge des incantations : temps serveur (GameState), sinon temps du monde. */
	float GetCastClockSeconds() const;

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

	TArray<FGameplayAbilitySpecHandle> GrantedAbilityHandles;
	TArray<FActiveGameplayEffectHandle> GrantedEffectHandles;
	FDelegateHandle MoveSpeedChangedHandle;
	FDelegateHandle OutOfHealthHandle;
};
