#include "CQTest.h"
#include "Components/PIENetworkComponent.h"

#if ENABLE_PIE_NETWORK_TEST

#include "Abilities/GameplayAbility.h"
#include "AbilitySystem/GenAbilitySystemComponent.h"
#include "Actors/GenProjectile.h"
#include "Character/GenPlayerCharacter.h"
#include "Engine/World.h"
#include "GameplayTagContainer.h"
#include "Net/GenNetTestHelpers.h"
#include "Player/GenPlayerController.h"
#include "Player/GenPlayerState.h"

using namespace GenNetTest;

/**
 * Gen.Net.CastLock : refus d'activation par State.CastLocked et par les contrôles durs
 * (UGenGameplayAbility::CanActivateAbility), sans le bond (il arrive en Task 8) : le verrou est
 * posé à la main comme le fera le bond (tag local + UGenAbilitySystemComponent::NoteCastLock).
 *
 * - Le côté qui prédit (client propriétaire) respecte toujours son propre verrou.
 * - Le serveur ne refuse un client distant que pendant LockDuration - CastTimeTolerance, puis accepte
 *   même si son propre verrou est encore posé (il finit ~½ RTT après celui du client).
 * - Un contrôle dur répliqué refuse l'activation côté client ; un contrôle dur que le client n'a pas encore
 *   reçu est refusé par le serveur (le client a prédit, sa prédiction est annulée).
 * - Chaque refus du serveur donne sa raison (tag dans les AbilityFailedCallbacks).
 * - Sans NoteCastLock, la fenêtre reste fermée : le serveur ne refuse rien (choix sûr, documenté).
 */
NETWORK_TEST_CLASS(CastLock, "Gen.Net")
{
	FPIENetworkComponent<FBasePIENetworkComponentState> Network{ TestRunner, TestCommandBuilder, bInitializing };

	TSubclassOf<UGameplayAbility> AbilityClass;
	TWeakObjectPtr<UWorld> ServerWorld;
	FDelegateHandle SpawnHandle;
	FDelegateHandle FailedHandle;

	/** Côté serveur : pion et ASC du lanceur (client 0). */
	TWeakObjectPtr<AGenPlayerCharacter> ServerCaster;
	TWeakObjectPtr<UGenAbilitySystemComponent> ServerCasterASC;

	int32 ServerProjectileCount = 0;
	int32 ServerFailureCount = 0;
	bool bServerLockedAtSpawn = false;
	/** Raisons du dernier refus du serveur (OptionalRelevantTags de CanActivateAbility). */
	FGameplayTagContainer ServerFailureTags;

	BEFORE_EACH()
	{
		IgnoreUntitledMapNetWarnings(*TestRunner);

		AbilityClass = LoadCurffeAbilityClass(TEXT("GA_Fireball"));
		ASSERT_THAT(IsNotNull(AbilityClass.Get(), TEXT("GA_Fireball introuvable")));

		FNetworkComponentBuilder<FBasePIENetworkComponentState>()
			.WithClients(2)
			.AsDedicatedServer()
			.WithGameMode(LoadGameModeClass())
			.Build(Network);
	}

	AFTER_EACH()
	{
		RemoveServerHandlers();
	}

	static FGameplayTag CastLockedTag()
	{
		// GenGameplayTags::* n'est pas exporté par le module Gen
		return FGameplayTag::RequestGameplayTag(TEXT("State.CastLocked"));
	}

	void RemoveServerHandlers()
	{
		if (ServerWorld.IsValid() && SpawnHandle.IsValid())
		{
			ServerWorld->RemoveOnActorSpawnedHandler(SpawnHandle);
		}
		SpawnHandle.Reset();
		if (ServerCasterASC.IsValid() && FailedHandle.IsValid())
		{
			ServerCasterASC->AbilityFailedCallbacks.Remove(FailedHandle);
		}
		FailedHandle.Reset();
	}

	/** Joueurs prêts, ennemi écarté, écoute des projectiles et des refus d'activation du lanceur côté serveur. */
	void QueuePrepare()
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
			.ThenServer(TEXT("Serveur : écarte l'ennemi et écoute le lanceur"), [this](FBasePIENetworkComponentState& Server)
			{
				ServerWorld = Server.World;

				AGenPlayerCharacter* Caster = GetServerController(Server, 0)->GetPawn<AGenPlayerCharacter>();
				AGenPlayerCharacter* Other = GetServerController(Server, 1)->GetPawn<AGenPlayerCharacter>();
				ASSERT_THAT(IsNotNull(Caster));
				ASSERT_THAT(IsNotNull(Other));
				ServerCaster = Caster;
				ServerCasterASC = Cast<UGenAbilitySystemComponent>(Caster->GetAbilitySystemComponent());
				ASSERT_THAT(IsNotNull(ServerCasterASC.Get(), TEXT("ASC Gen attendu")));

				Other->TeleportTo(Caster->GetActorLocation() + FVector(0.f, 5000.f, 0.f), Other->GetActorRotation(), false, true);

				SpawnHandle = Server.World->AddOnActorSpawnedHandler(FOnActorSpawned::FDelegate::CreateLambda([this](AActor* Actor)
				{
					const AGenProjectile* Projectile = Cast<AGenProjectile>(Actor);
					if (Projectile && ServerCaster.IsValid() && Projectile->GetInstigator() == ServerCaster.Get())
					{
						++ServerProjectileCount;
						bServerLockedAtSpawn = ServerCasterASC.IsValid() && ServerCasterASC->HasMatchingGameplayTag(CastLockedTag());
					}
				}));

				FailedHandle = ServerCasterASC->AbilityFailedCallbacks.AddLambda([this](const UGameplayAbility* Ability, const FGameplayTagContainer& FailureTags)
				{
					if (Ability && Ability->GetClass() == AbilityClass.Get())
					{
						++ServerFailureCount;
						ServerFailureTags = FailureTags;
					}
				});
			});
	}

	/** Serveur : pose le verrou comme le bond (tag local + fenêtre sur la durée minimale du verrou). */
	void QueueServerLock(float MinLockDuration)
	{
		Network.ThenServer(TEXT("Serveur : pose State.CastLocked et note la fenêtre"), [this, MinLockDuration](FBasePIENetworkComponentState&)
		{
			ASSERT_THAT(IsNotNull(ServerCasterASC.Get()));
			ServerCasterASC->AddLooseGameplayTag(CastLockedTag());
			ServerCasterASC->NoteCastLock(MinLockDuration);
		});
	}

	/** Serveur : le sort du client doit être refusé, sans tir, pour la raison Reason ; puis le client annule sa prédiction. */
	void QueueExpectServerRefusal(const FGameplayTag& Reason)
	{
		Network
			.UntilServer(TEXT("Serveur : activation refusée"), [this](FBasePIENetworkComponentState&)
			{
				return ServerFailureCount > 0;
			}, DefaultWait())
			.ThenServer(TEXT("Serveur : aucun tir, sort inactif, raison du refus"), [this, Reason](FBasePIENetworkComponentState&)
			{
				ASSERT_THAT(AreEqual(0, ServerProjectileCount, TEXT("Aucun projectile ne doit partir après un refus")));
				const FGameplayAbilitySpec* Spec = FindAbilitySpec(ServerCasterASC.Get(), AbilityClass);
				ASSERT_THAT(IsTrue(Spec && !Spec->IsActive(), TEXT("Le sort ne doit pas être actif sur le serveur")));
				ASSERT_THAT(IsTrue(ServerFailureTags.HasTagExact(Reason), *FString::Printf(TEXT("Raison du refus attendue : %s (reçu : %s)"), *Reason.ToString(), *ServerFailureTags.ToStringSimple())));
				RemoveServerHandlers();
			})
			.UntilClient(TEXT("Client 0 : prédiction annulée (sort terminé)"), 0, [this](FBasePIENetworkComponentState& Client)
			{
				const FGameplayAbilitySpec* Spec = FindAbilitySpec(GetASC(GetLocalController(Client)->GetPlayerState<AGenPlayerState>()), AbilityClass);
				return Spec && !Spec->IsActive();
			}, DefaultWait());
	}

	/** Client 0 : active le sort par son ASC (prédiction locale), visée déterministe. */
	void QueueClientActivate(bool bExpectLocalSuccess)
	{
		Network.ThenClient(TEXT("Client 0 : active la boule de feu"), 0, [this, bExpectLocalSuccess](FBasePIENetworkComponentState& Client)
		{
			AGenPlayerController* PC = Cast<AGenPlayerController>(GetLocalController(Client));
			ASSERT_THAT(IsNotNull(PC));
			const AGenPlayerCharacter* Pawn = PC->GetPawn<AGenPlayerCharacter>();
			ASSERT_THAT(IsNotNull(Pawn));
			PC->bDebugAimOverride = true;
			PC->DebugAimLocation = Pawn->GetActorLocation() + FVector(1000.f, 0.f, 0.f);

			UAbilitySystemComponent* ASC = GetASC(PC->GetPlayerState<AGenPlayerState>());
			FGameplayAbilitySpec* Spec = FindAbilitySpec(ASC, AbilityClass);
			ASSERT_THAT(IsNotNull(Spec, TEXT("Le client doit avoir reçu le spec du sort")));
			const bool bActivated = ASC->TryActivateAbility(Spec->Handle, /*bAllowRemoteActivation*/ true);
			ASSERT_THAT(IsTrue(bActivated == bExpectLocalSuccess, bExpectLocalSuccess
				? TEXT("Le client (sans verrou local) doit prédire l'activation")
				: TEXT("Le client doit refuser l'activation localement")));
		});
	}

	/** Le serveur a posé son verrou au tout début : la boule de feu d'un client est refusée et ne coûte rien. */
	TEST_METHOD(ServerWindow_RefusesActivationEarlyInTheLock)
	{
		QueuePrepare();
		QueueServerLock(10.f);
		QueueClientActivate(/*bExpectLocalSuccess*/ true);
		QueueExpectServerRefusal(CastLockedTag());
	}

	/**
	 * Verrou posé sans NoteCastLock (ce que SetCastLock interdit) : la fenêtre reste fermée et le serveur accepte.
	 * Documente le choix « échec ouvert » : un oubli ne bloque jamais un client honnête, seul son propre verrou compte.
	 */
	TEST_METHOD(LockWithoutNoteCastLock_ServerAccepts)
	{
		QueuePrepare();

		Network.ThenServer(TEXT("Serveur : pose State.CastLocked sans noter de fenêtre"), [this](FBasePIENetworkComponentState&)
		{
			ASSERT_THAT(IsNotNull(ServerCasterASC.Get()));
			ServerCasterASC->AddLooseGameplayTag(CastLockedTag());
			ASSERT_THAT(IsTrue(ServerCasterASC->GetCastLockEnforcedUntil() < 0.0, TEXT("Fenêtre fermée par défaut")));
		});

		QueueClientActivate(/*bExpectLocalSuccess*/ true);

		Network
			.UntilServer(TEXT("Serveur : projectile du lanceur apparu"), [this](FBasePIENetworkComponentState&)
			{
				return ServerProjectileCount > 0;
			}, DefaultWait())
			.ThenServer(TEXT("Serveur : accepté malgré son tag"), [this](FBasePIENetworkComponentState&)
			{
				ASSERT_THAT(AreEqual(0, ServerFailureCount, TEXT("Sans fenêtre, aucun refus")));
				ASSERT_THAT(IsTrue(bServerLockedAtSpawn, TEXT("Le tag du serveur doit être posé : sinon le test ne vérifie rien")));
				ServerCasterASC->RemoveLooseGameplayTag(CastLockedTag());
				RemoveServerHandlers();
			});
	}

	/**
	 * Contrôle dur que le client n'a pas encore reçu (ici : tag libre du serveur, jamais répliqué, pour figer
	 * la course) : le client prédit, le serveur refuse avec le tag du contrôle comme raison, la prédiction est annulée.
	 */
	TEST_METHOD(HardCC_NotYetReplicated_ServerRefuses)
	{
		QueuePrepare();

		Network.ThenServer(TEXT("Serveur : silence connu du serveur seul"), [this](FBasePIENetworkComponentState&)
		{
			ASSERT_THAT(IsNotNull(ServerCasterASC.Get()));
			ServerCasterASC->AddLooseGameplayTag(FGameplayTag::RequestGameplayTag(TEXT("State.Silenced")));
		});

		QueueClientActivate(/*bExpectLocalSuccess*/ true);
		QueueExpectServerRefusal(FGameplayTag::RequestGameplayTag(TEXT("State.Silenced")));

		Network.ThenServer(TEXT("Serveur : retire le silence"), [this](FBasePIENetworkComponentState&)
		{
			if (ServerCasterASC.IsValid())
			{
				ServerCasterASC->RemoveLooseGameplayTag(FGameplayTag::RequestGameplayTag(TEXT("State.Silenced")));
			}
		});
	}

	/**
	 * Verrou de 0.45 s (bond) : la fenêtre serveur se ferme à 0.35 s. Une activation reçue après, alors que
	 * le verrou du serveur est encore posé (son atterrissage arrive ~½ RTT après celui du client), est acceptée.
	 */
	TEST_METHOD(ServerWindow_AcceptsActivationAfterTheWindow)
	{
		QueuePrepare();
		QueueServerLock(0.45f);

		Network
			.UntilServer(TEXT("Serveur : fenêtre du verrou écoulée"), [this](FBasePIENetworkComponentState& Server)
			{
				return ServerCasterASC.IsValid() && Server.World->GetTimeSeconds() >= ServerCasterASC->GetCastLockEnforcedUntil();
			}, DefaultWait());

		QueueClientActivate(/*bExpectLocalSuccess*/ true);

		Network
			.UntilServer(TEXT("Serveur : projectile du lanceur apparu"), [this](FBasePIENetworkComponentState&)
			{
				return ServerProjectileCount > 0;
			}, DefaultWait())
			.ThenServer(TEXT("Serveur : accepté sous son propre verrou"), [this](FBasePIENetworkComponentState&)
			{
				ASSERT_THAT(AreEqual(1, ServerProjectileCount));
				ASSERT_THAT(AreEqual(0, ServerFailureCount, TEXT("Aucun refus attendu après la fenêtre")));
				ASSERT_THAT(IsTrue(bServerLockedAtSpawn, TEXT("Le verrou du serveur doit encore être posé : sinon le test ne vérifie pas la fenêtre")));
				ServerCasterASC->RemoveLooseGameplayTag(CastLockedTag());
				RemoveServerHandlers();
			});
	}

	/** Le côté qui prédit fait foi : sous son propre verrou, le client refuse lui-même l'activation. */
	TEST_METHOD(PredictingSide_RefusesUnderItsOwnLock)
	{
		QueuePrepare();

		Network.ThenClient(TEXT("Client 0 : pose son verrou local"), 0, [this](FBasePIENetworkComponentState& Client)
		{
			UAbilitySystemComponent* ASC = GetASC(GetLocalController(Client)->GetPlayerState<AGenPlayerState>());
			ASSERT_THAT(IsNotNull(ASC));
			ASC->AddLooseGameplayTag(CastLockedTag());
		});

		QueueClientActivate(/*bExpectLocalSuccess*/ false);

		Network.ThenClient(TEXT("Client 0 : retire son verrou, le sort redevient activable"), 0, [this](FBasePIENetworkComponentState& Client)
		{
			UAbilitySystemComponent* ASC = GetASC(GetLocalController(Client)->GetPlayerState<AGenPlayerState>());
			ASSERT_THAT(IsNotNull(ASC));
			ASC->RemoveLooseGameplayTag(CastLockedTag());
			const FGameplayAbilitySpec* Spec = FindAbilitySpec(ASC, AbilityClass);
			ASSERT_THAT(IsNotNull(Spec));
			const UGameplayAbility* CDO = Spec->Ability;
			ASSERT_THAT(IsTrue(CDO && CDO->CanActivateAbility(Spec->Handle, ASC->AbilityActorInfo.Get()), TEXT("Sans verrou, le sort doit être activable")));
		});
	}

	/** Contrôle dur (silence) appliqué par le serveur : une fois répliqué, le client refuse l'activation. */
	TEST_METHOD(HardCC_Silence_RefusesActivationOnTheClient)
	{
		QueuePrepare();

		Network
			.ThenServer(TEXT("Serveur : réduit le client 0 au silence"), [this](FBasePIENetworkComponentState&)
			{
				ASSERT_THAT(IsNotNull(ServerCasterASC.Get()));
				const FActiveGameplayEffectHandle Handle = ServerCasterASC->ApplyHardCC(FGameplayTag::RequestGameplayTag(TEXT("State.Silenced")), 10.f, nullptr);
				ASSERT_THAT(IsTrue(Handle.IsValid(), TEXT("ApplyHardCC doit appliquer le silence")));
			})
			.UntilClient(TEXT("Client 0 : silence répliqué"), 0, [](FBasePIENetworkComponentState& Client)
			{
				const UAbilitySystemComponent* ASC = GetASC(GetLocalController(Client)->GetPlayerState<AGenPlayerState>());
				return ASC && ASC->HasMatchingGameplayTag(FGameplayTag::RequestGameplayTag(TEXT("State.Silenced")));
			}, DefaultWait());

		QueueClientActivate(/*bExpectLocalSuccess*/ false);
	}
};

#endif // ENABLE_PIE_NETWORK_TEST
