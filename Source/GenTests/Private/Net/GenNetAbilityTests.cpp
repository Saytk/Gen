#include "CQTest.h"
#include "Components/PIENetworkComponent.h"

#if ENABLE_PIE_NETWORK_TEST

#include "Abilities/GameplayAbility.h"
#include "AbilitySystem/Abilities/GenGameplayAbility.h"
#include "AbilitySystem/GenAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "Actors/GenProjectile.h"
#include "Character/GenPlayerCharacter.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Net/GenNetTestHelpers.h"
#include "Player/GenPlayerController.h"
#include "Player/GenPlayerState.h"

using namespace GenNetTest;

/**
 * Gen.Net.ClientAbility : le client 0 active un sort de projectile de Curffe par son ASC
 * (TryActivateAbility, sans injection d'input), comme le ferait un appui de touche. Flux complet :
 * activation prédite côté client -> ServerTryActivateAbility -> [nourrissage à 0 flamme : la touche
 * n'est pas tenue -> annonce + synchro] -> incantation -> visée envoyée en target data
 * -> CommitAbility serveur (cooldown) -> projectile répliqué -> fin du sort répliquée par le serveur.
 */
NETWORK_TEST_CLASS(ClientAbility, "Gen.Net")
{
	FPIENetworkComponent<FBasePIENetworkComponentState> Network{ TestRunner, TestCommandBuilder, bInitializing };

	TSubclassOf<UGameplayAbility> AbilityClass;
	TWeakObjectPtr<UWorld> ServerWorld;
	FDelegateHandle SpawnHandle;

	/** Côté serveur : pion et ASC du lanceur (client 0). */
	TWeakObjectPtr<AGenPlayerCharacter> ServerCaster;
	TWeakObjectPtr<UAbilitySystemComponent> ServerCasterASC;

	/** Relevés faits au moment où le serveur fait apparaître le projectile du lanceur. */
	int32 ServerProjectileCount = 0;
	bool bCooldownAppliedAtSpawn = false;

	BEFORE_EACH()
	{
		IgnoreUntitledMapNetWarnings(*TestRunner);

		FNetworkComponentBuilder<FBasePIENetworkComponentState>()
			.WithClients(2)
			.AsDedicatedServer()
			.WithGameMode(LoadGameModeClass())
			.Build(Network);
	}

	AFTER_EACH()
	{
		// Filet de sécurité si le test a échoué avant de retirer le handler
		if (ServerWorld.IsValid() && SpawnHandle.IsValid())
		{
			ServerWorld->RemoveOnActorSpawnedHandler(SpawnHandle);
		}
		SpawnHandle.Reset();
	}

	const FGameplayTagContainer* GetCooldownTags() const
	{
		const UGenGameplayAbility* CDO = AbilityClass ? Cast<UGenGameplayAbility>(AbilityClass->GetDefaultObject()) : nullptr;
		return CDO ? CDO->GetCooldownTags() : nullptr;
	}

	/** Le sort a-t-il un cooldown à appliquer au lancer ? */
	bool AbilityHasCooldown() const
	{
		const UGenGameplayAbility* CDO = AbilityClass ? Cast<UGenGameplayAbility>(AbilityClass->GetDefaultObject()) : nullptr;
		const FGameplayTagContainer* CooldownTags = GetCooldownTags();
		return CDO && CooldownTags && !CooldownTags->IsEmpty() && CDO->CooldownDuration.GetValueAtLevel(1) > 0.f;
	}

	/**
	 * Met en file tout le scénario : joueurs prêts -> le client 0 lance AbilityClass -> le serveur fait
	 * apparaître exactement un projectile du lanceur (cooldown déjà appliqué s'il y en a un) -> le client
	 * le reçoit -> le sort se termine sur le serveur et sur le client.
	 */
	void QueueClientCast()
	{
		Network
			.UntilServer(TEXT("Serveur : joueurs prêts"), [](FBasePIENetworkComponentState& Server)
			{
				return AreAllServerPlayersReady(Server);
			}, DefaultWait())
			.UntilClients(TEXT("Clients : joueurs prêts"), [](FBasePIENetworkComponentState& Client)
			{
				return IsPlayerReady(GetLocalController(Client));
			}, DefaultWait())
			.ThenServer(TEXT("Serveur : écarte la cible et écoute les apparitions"), [this](FBasePIENetworkComponentState& Server)
			{
				ServerWorld = Server.World;

				AGenPlayerCharacter* Caster = GetServerController(Server, 0)->GetPawn<AGenPlayerCharacter>();
				AGenPlayerCharacter* Other = GetServerController(Server, 1)->GetPawn<AGenPlayerCharacter>();
				ASSERT_THAT(IsNotNull(Caster));
				ASSERT_THAT(IsNotNull(Other));
				ServerCaster = Caster;
				ServerCasterASC = Caster->GetAbilitySystemComponent();
				ASSERT_THAT(IsNotNull(FindAbilitySpec(ServerCasterASC.Get(), AbilityClass), TEXT("Le sort doit être accordé par BP_Curffe")));

				// Carte vide : les deux pions apparaissent à l'origine. On éloigne l'autre joueur (ennemi)
				// pour que le projectile ne le touche pas dès son apparition.
				Other->TeleportTo(Caster->GetActorLocation() + FVector(0.f, 5000.f, 0.f), Other->GetActorRotation(), false, true);

				SpawnHandle = Server.World->AddOnActorSpawnedHandler(FOnActorSpawned::FDelegate::CreateLambda([this](AActor* Actor)
				{
					const AGenProjectile* Projectile = Cast<AGenProjectile>(Actor);
					if (Projectile && ServerCaster.IsValid() && Projectile->GetInstigator() == ServerCaster.Get())
					{
						++ServerProjectileCount;
						// CommitAbility (cooldown, coût) précède l'apparition du projectile (ReleaseShot puis LaunchShot)
						const FGameplayTagContainer* CooldownTags = GetCooldownTags();
						bCooldownAppliedAtSpawn = ServerCasterASC.IsValid() && CooldownTags && ServerCasterASC->HasAnyMatchingGameplayTags(*CooldownTags);
					}
				}));
			})
			.ThenClient(TEXT("Client 0 : active le sort (prédiction locale)"), 0, [this](FBasePIENetworkComponentState& Client)
			{
				AGenPlayerController* PC = Cast<AGenPlayerController>(GetLocalController(Client));
				ASSERT_THAT(IsNotNull(PC));
				const AGenPlayerCharacter* Pawn = PC->GetPawn<AGenPlayerCharacter>();
				ASSERT_THAT(IsNotNull(Pawn));

				// Visée déterministe : pas de souris en test, on vise 10 m devant le pion (+X)
				PC->bDebugAimOverride = true;
				PC->DebugAimLocation = Pawn->GetActorLocation() + FVector(1000.f, 0.f, 0.f);

				UAbilitySystemComponent* ASC = GetASC(PC->GetPlayerState<AGenPlayerState>());
				FGameplayAbilitySpec* Spec = FindAbilitySpec(ASC, AbilityClass);
				ASSERT_THAT(IsNotNull(Spec, TEXT("Le client doit avoir reçu le spec du sort")));
				ASSERT_THAT(IsTrue(ASC->TryActivateAbility(Spec->Handle, /*bAllowRemoteActivation*/ true), TEXT("TryActivateAbility refusé côté client")));
			})
			.UntilServer(TEXT("Serveur : projectile du lanceur apparu"), [this](FBasePIENetworkComponentState&)
			{
				return ServerProjectileCount > 0;
			}, DefaultWait())
			.ThenServer(TEXT("Serveur : un seul tir, cooldown appliqué"), [this](FBasePIENetworkComponentState& Server)
			{
				Server.World->RemoveOnActorSpawnedHandler(SpawnHandle);
				SpawnHandle.Reset();

				ASSERT_THAT(AreEqual(1, ServerProjectileCount));
				if (AbilityHasCooldown())
				{
					ASSERT_THAT(IsTrue(bCooldownAppliedAtSpawn, TEXT("Le cooldown doit être appliqué par le serveur au lancer")));
				}
			})
			.UntilClient(TEXT("Client 0 : le projectile répliqué arrive"), 0, [](FBasePIENetworkComponentState& Client)
			{
				const APawn* LocalPawn = GetLocalController(Client)->GetPawn();
				for (TActorIterator<AGenProjectile> It(Client.World); It; ++It)
				{
					if (It->GetInstigator() == LocalPawn)
					{
						return true;
					}
				}
				return false;
			}, DefaultWait())
			.UntilServer(TEXT("Serveur : sort terminé"), [this](FBasePIENetworkComponentState&)
			{
				const FGameplayAbilitySpec* Spec = FindAbilitySpec(ServerCasterASC.Get(), AbilityClass);
				return Spec && !Spec->IsActive();
			}, DefaultWait())
			.UntilClient(TEXT("Client 0 : sort terminé (fin répliquée par le serveur)"), 0, [this](FBasePIENetworkComponentState& Client)
			{
				const FGameplayAbilitySpec* Spec = FindAbilitySpec(GetASC(GetLocalController(Client)->GetPlayerState<AGenPlayerState>()), AbilityClass);
				return Spec && !Spec->IsActive();
			}, DefaultWait());
	}

	/** M1, sans cooldown : incantation courte puis tir. */
	TEST_METHOD(Fireball_ClientActivation_ServerSpawnsProjectile)
	{
		AbilityClass = LoadCurffeAbilityClass(TEXT("GA_Fireball"));
		ASSERT_THAT(IsNotNull(AbilityClass.Get(), TEXT("GA_Fireball introuvable")));
		QueueClientCast();
	}

	/** M2, nourrissable + cooldown : la touche n'étant pas tenue, 0 flamme nourrie puis incantation. */
	TEST_METHOD(GreatFireball_ClientActivation_ServerAppliesCooldown)
	{
		AbilityClass = LoadCurffeAbilityClass(TEXT("GA_GreatFireball"));
		ASSERT_THAT(IsNotNull(AbilityClass.Get(), TEXT("GA_GreatFireball introuvable")));
		ASSERT_THAT(IsTrue(AbilityHasCooldown(), TEXT("GA_GreatFireball doit avoir un cooldown (sinon ce test ne vérifie plus le commit)")));

		QueueClientCast();

		Network
			.ThenClient(TEXT("Client 0 : le cooldown est actif (prédit puis confirmé par le serveur)"), 0, [this](FBasePIENetworkComponentState& Client)
			{
				const UAbilitySystemComponent* ASC = GetASC(GetLocalController(Client)->GetPlayerState<AGenPlayerState>());
				ASSERT_THAT(IsNotNull(ASC));
				ASSERT_THAT(IsTrue(ASC->HasAnyMatchingGameplayTags(*GetCooldownTags()), TEXT("Le client doit voir le cooldown du sort")));
			})
			.ThenServer(TEXT("Serveur : aucune flamme dépensée (rien n'a été nourri)"), [this](FBasePIENetworkComponentState&)
			{
				// La régénération du Foyer ne peut qu'ajouter : le Foyer reste plein
				ASSERT_THAT(IsNear(GetAttribute(ServerCasterASC.Get(), UGenAttributeSet::GetMaxResourceAttribute()),
					GetAttribute(ServerCasterASC.Get(), UGenAttributeSet::GetResourceAttribute()), 0.01f));
			});
	}
};

#endif // ENABLE_PIE_NETWORK_TEST
