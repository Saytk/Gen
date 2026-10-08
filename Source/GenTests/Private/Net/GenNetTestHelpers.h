#pragma once

#include "CQTest.h"
#include "Components/PIENetworkComponent.h"

#if ENABLE_PIE_NETWORK_TEST

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Templates/SubclassOf.h"

class ACharacter;
class AGameModeBase;
class AGenPlayerCharacter;
class AGenPlayerState;
class APlayerController;
class UAbilitySystemComponent;
class UGameplayAbility;
class UWorld;
struct FGameplayAbilitySpec;
struct FGameplayAttribute;

/**
 * Outils partagés des tests réseau Gen.Net.* (serveur dédié + N clients dans un seul process).
 *
 * Rappels sur FPIENetworkComponent (CQTest, UE 5.8) :
 * - Chaque TEST_METHOD relance une session PIE complète sur une NOUVELLE carte vide
 *   (FAutomationEditorCommonUtils::CreateNewMap) : pas de sol, pas de PlayerStart, les pions
 *   apparaissent à l'origine et tombent. Ne pas compter sur leur position.
 * - FBasePIENetworkComponentState::World = monde PIE du serveur ou du client ;
 *   ServerState.ClientConnections[i] = connexion serveur du client i (son PlayerController serveur).
 */
namespace GenNetTest
{
	/** Mode de jeu réel du projet (BP_GenGameMode : pion BP_Curffe, contrôleur BP_GenPlayerController). */
	TSubclassOf<AGameModeBase> LoadGameModeClass();

	/** Classe Blueprint d'un sort de Curffe, par nom d'asset (ex : TEXT("GA_Fireball")). */
	TSubclassOf<UGameplayAbility> LoadCurffeAbilityClass(const TCHAR* AssetName);

	/**
	 * Ignore les avertissements LogNetPackageMap propres à la carte "Untitled" non sauvegardée que
	 * crée FPIENetworkComponent (niveau /Temp/... non supporté par le cache de NetGUID, WorldSettings
	 * enregistré par chemin). Sans danger, mais sinon chaque test finit en "Success with warnings".
	 * À appeler dans BEFORE_EACH : IgnoreUntitledMapNetWarnings(*TestRunner);
	 */
	void IgnoreUntitledMapNetWarnings(FAutomationTestBase& Test);

	/** Attente maximale par défaut d'une condition réseau (.Until*). */
	inline FTimespan DefaultWait() { return FTimespan::FromSeconds(15.0); }

	/** PlayerController du client ClientIndex vu par le serveur (nullptr tant que la connexion n'a pas de PC). */
	APlayerController* GetServerController(const FBasePIENetworkComponentState& ServerState, int32 ClientIndex);

	/** PlayerController local d'un monde client. */
	APlayerController* GetLocalController(const FBasePIENetworkComponentState& ClientState);

	/** PlayerState d'identifiant PlayerId dans un monde donné (serveur ou client), sinon nullptr. */
	AGenPlayerState* FindPlayerStateById(const UWorld* World, int32 PlayerId);

	/** ASC porté par le PlayerState (nullptr si PS nul). */
	UAbilitySystemComponent* GetASC(const AGenPlayerState* PlayerState);

	/**
	 * Joueur prêt : pion AGenPlayerCharacter possédé, PlayerState Gen, ASC dont l'avatar est ce pion
	 * et sorts de départ présents (accordés sur le serveur, répliqués au client propriétaire).
	 */
	bool IsPlayerReady(const APlayerController* PlayerController);

	/** Tous les clients ont un joueur prêt côté serveur. */
	bool AreAllServerPlayersReady(const FBasePIENetworkComponentState& ServerState);

	/** Valeur courante d'un attribut (0 si ASC nul). */
	float GetAttribute(const UAbilitySystemComponent* ASC, const FGameplayAttribute& Attribute);

	/**
	 * Compare les attributs répliqués (Health, MaxHealth, Energy, MaxEnergy, Resource, MaxResource, MoveSpeed)
	 * de deux ASC. Faux à la première différence, décrite dans OutDiff si fourni.
	 */
	bool DoReplicatedAttributesMatch(const UAbilitySystemComponent* Expected, const UAbilitySystemComponent* Actual, FString* OutDiff = nullptr);

	/** Spec du sort de classe AbilityClass dans l'ASC, sinon nullptr. */
	FGameplayAbilitySpec* FindAbilitySpec(UAbilitySystemComponent* ASC, TSubclassOf<UGameplayAbility> AbilityClass);

	/**
	 * Serveur : sol plat de test répliqué (AGenNetTestFloor, surface à Z = 0) ; la carte vide n'en a pas. Attendre ensuite
	 * que chaque client l'ait reçu (HasTestFloor) avant de poser les pions dessus.
	 */
	AActor* SpawnTestFloor(UWorld* ServerWorld);

	/** Le sol de test est présent dans ce monde (client : reçu du serveur). */
	bool HasTestFloor(const UWorld* World);

	/** Hauteur où poser un pion de Curffe debout sur le sol de test (centre de sa capsule, un peu au-dessus). */
	inline constexpr float StandingHeight = 100.f;

	// Outils communs des tests de sorts (revue finale, M-6 : une seule définition, sans conflit en build unity)

	/** Tag par son nom (les tags de Gen et de Curffe ne sont pas exportés par le module Gen). */
	FGameplayTag Tag(const TCHAR* Name);

	/** Serveur : dégâts de Source sur Target (GE de dégâts du projet). */
	void ApplyDamage(UAbilitySystemComponent* Source, UAbilitySystemComponent* Target, float Amount);

	/** Serveur : gain (ou perte si négatif) d'énergie et de ressource. */
	void ApplyGain(UAbilitySystemComponent* ASC, float Energy, float Resource);

	/** Pose le pion debout sur le sol de test en (X, Y). */
	void PlaceOnFloor(ACharacter* Character, float X, float Y);

	/** Le serveur a lancé le personnage (repoussement) dans cette image. */
	bool IsBeingLaunched(const ACharacter* Character);
}

#endif // ENABLE_PIE_NETWORK_TEST
